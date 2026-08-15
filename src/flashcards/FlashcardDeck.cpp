// src/flashcards/FlashcardDeck.cpp
#include "FlashcardDeck.h"

#include <HalStorage.h>              // Storage singleton + HalFile
#include <common/FsApiConstants.h>  // O_WRONLY / O_CREAT / O_APPEND (oflag_t)
#include <Logging.h>                // LOG_ERR / LOG_INF

#include <algorithm>
#include <cstring>

namespace {

constexpr const char* MOD = "FLC";  // Storage log tag

// FNV-1a over front + '\t' + back → stable 32-bit card identity. Independent of
// line position, so editing/reordering the .tsv preserves scheduling.
uint32_t cardHash(const std::string& front, const std::string& back) {
  uint32_t h = 2166136261u;
  auto mix = [&](const std::string& s) {
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
  };
  mix(front);
  h ^= '\t'; h *= 16777619u;
  mix(back);
  return h ? h : 1;  // never collide with the 0 "empty" sentinel
}

// Read one '\n'-terminated line from an open HalFile into `out` (without the newline).
// Returns false at EOF with nothing read. Trailing '\r' is stripped.
bool readLine(HalFile& f, std::string& out) {
  out.clear();
  char c;
  int n;
  bool any = false;
  while ((n = f.read(&c, 1)) == 1) {
    any = true;
    if (c == '\n') break;
    if (c != '\r') out.push_back(c);
  }
  return any || n == 1;
}

// Replace tabs/newlines in appended fields so one card always stays one line.
std::string sanitizeField(const std::string& s) {
  std::string o = s;
  for (char& c : o)
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return o;
}

// Extract the first two fields of a deck line as front/back. The delimiter is a
// tab when the line has one (spreadsheet/Anki-style .tsv), otherwise a comma
// (.csv). Fields are parsed RFC-4180-style, so double-quoted values keep their
// embedded delimiters and "" un-escapes to a single quote. Any columns past the
// first two (e.g. a deck that stores its own SM-2 stats: Repetitions,
// EasinessFactor, Interval, NextReviewSession) are ignored. Returns false when
// the line has no delimiter at all.
bool splitCardLine(const std::string& line, std::string& front, std::string& back) {
  front.clear();
  back.clear();

  const char delim = line.find('\t') != std::string::npos ? '\t' : ',';
  bool inQuotes = false;
  int field = 0;  // 0 = front, 1 = back, 2 = reached the third column (stop)
  bool sawDelim = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    std::string& dst = field == 0 ? front : back;
    if (inQuotes) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {  // "" -> literal quote
          if (field < 2) dst += '"';
          ++i;
        } else {
          inQuotes = false;
        }
      } else if (field < 2) {
        dst += c;
      }
    } else if (c == '"') {
      inQuotes = true;
    } else if (c == delim) {
      sawDelim = true;
      if (++field >= 2) break;  // front + back captured; ignore any extra columns
    } else if (field < 2) {
      dst += c;
    }
  }
  return sawDelim;  // need at least one delimiter to have both a front and a back
}

std::string srsPathFor(const std::string& tsvPath) {
  auto dot = tsvPath.find_last_of('.');
  return (dot == std::string::npos ? tsvPath : tsvPath.substr(0, dot)) + ".srs";
}

}  // namespace

FlashcardDeck::SrsRec* FlashcardDeck::findRec(uint32_t hash) {
  for (auto& r : srs_)
    if (r.hash == hash) return &r;
  return nullptr;
}

bool FlashcardDeck::load(const std::string& tsvPath, uint32_t todayEpochDay) {
  tsvPath_ = tsvPath;
  srsPath_ = srsPathFor(tsvPath);
  due_.clear();
  srs_.clear();
  cursor_ = reviewed_ = totalCount_ = 0;
  overflowed_ = false;

  // ---- 1. Load the sidecar (all scheduling state) if present -------------
  // Sidecar is a flat array of fixed 14-byte little-endian records:
  //   hash:u32, due:u32, intervalDays:u16, reps:u16, ease:u16
  if (Storage.exists(srsPath_.c_str())) {
    HalFile sf;
    if (Storage.openFileForRead(MOD, srsPath_, sf)) {
      uint8_t rec[14];
      while (sf.read(rec, sizeof(rec)) == static_cast<int>(sizeof(rec))) {
        SrsRec r;
        std::memcpy(&r.hash, rec + 0, 4);
        std::memcpy(&r.due, rec + 4, 4);
        std::memcpy(&r.srs.intervalDays, rec + 8, 2);
        std::memcpy(&r.srs.reps, rec + 10, 2);
        std::memcpy(&r.srs.ease, rec + 12, 2);
        srs_.push_back(r);
      }
      sf.close();
    }
  }

  // ---- 2. Stream the .tsv, build the due-today queue ---------------------
  HalFile f;
  if (!Storage.openFileForRead(MOD, tsvPath_, f)) {
    LOG_ERR(MOD, "Cannot open deck: %s", tsvPath_.c_str());
    return false;
  }

  std::string line, front, back;
  while (readLine(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (!splitCardLine(line, front, back)) continue;         // no tab or comma separator
    if (front == "Front" && back == "Back") continue;        // spreadsheet/Anki header row
    totalCount_++;

    Card c;
    c.front = front;
    c.back = back;
    c.hash = cardHash(c.front, c.back);

    if (SrsRec* r = findRec(c.hash)) {
      c.srs = r->srs;
      c.due = r->due;
    } else {
      c.srs = SrsState{};  // new card: defaults, due immediately
      c.due = 0;
    }

    const bool isDue = c.due <= todayEpochDay;  // due==0 (new) is always due
    if (!isDue) continue;
    if (due_.size() >= MAX_DUE) { overflowed_ = true; continue; }
    due_.push_back(std::move(c));
  }
  f.close();

  LOG_INF(MOD, "Deck %s: %u cards, %u due%s", tsvPath_.c_str(), (unsigned)totalCount_,
          (unsigned)due_.size(), overflowed_ ? " (capped)" : "");
  return true;
}

const FlashcardDeck::Card* FlashcardDeck::current() const {
  return cursor_ < due_.size() ? &due_[cursor_] : nullptr;
}

void FlashcardDeck::grade(int quality, uint32_t todayEpochDay) {
  if (cursor_ >= due_.size()) return;
  Card& c = due_[cursor_];

  uint16_t interval = sm2Advance(c.srs, quality);
  c.due = todayEpochDay + interval;

  // Upsert the scheduling record so the sidecar rewrite is complete.
  if (SrsRec* r = findRec(c.hash)) {
    r->srs = c.srs;
    r->due = c.due;
  } else {
    srs_.push_back(SrsRec{c.hash, c.due, c.srs});
  }

  saveSidecar();
  reviewed_++;
  cursor_++;
}

bool FlashcardDeck::saveSidecar() {
  // Crash-safe: write the full record set to a temp file, then rename over the
  // canonical one. An interrupted write damages only the throwaway temp — the
  // same pattern the reader uses for progress.bin. A torn sidecar would else
  // read as corrupt scheduling state.
  const std::string tmp = srsPath_ + ".tmp";
  {
    HalFile f;
    if (!Storage.openFileForWrite(MOD, tmp, f)) {
      LOG_ERR(MOD, "Cannot open sidecar temp: %s", tmp.c_str());
      return false;
    }
    uint8_t rec[14];
    for (const SrsRec& r : srs_) {
      std::memcpy(rec + 0, &r.hash, 4);
      std::memcpy(rec + 4, &r.due, 4);
      std::memcpy(rec + 8, &r.srs.intervalDays, 2);
      std::memcpy(rec + 10, &r.srs.reps, 2);
      std::memcpy(rec + 12, &r.srs.ease, 2);
      if (f.write(rec, sizeof(rec)) != sizeof(rec)) {
        LOG_ERR(MOD, "Short write to sidecar temp");
        f.close();
        return false;
      }
    }
    f.flush();
    f.close();
  }
  Storage.remove(srsPath_.c_str());  // SdFat rename won't overwrite; drop first
  if (!Storage.rename(tmp.c_str(), srsPath_.c_str())) {
    LOG_ERR(MOD, "Cannot rename sidecar into place");
    return false;
  }
  return true;
}

bool FlashcardDeck::appendCard(const std::string& tsvPath, const std::string& front, const std::string& back) {
  Storage.mkdir("/flashcards");  // no-op if it exists
  const std::string line = sanitizeField(front) + "\t" + sanitizeField(back) + "\n";
  HalFile f = Storage.open(tsvPath.c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!f) {
    LOG_ERR(MOD, "Cannot open deck for append: %s", tsvPath.c_str());
    return false;
  }
  const bool ok = f.write(line.c_str(), line.size()) == line.size();
  f.flush();
  f.close();
  return ok;
}
