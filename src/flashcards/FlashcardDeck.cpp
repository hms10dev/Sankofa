// src/flashcards/FlashcardDeck.cpp
#include "FlashcardDeck.h"

#include <HalStorage.h>            // Storage singleton + HalFile
#include <Logging.h>              // LOG_ERR / LOG_DBG

#include <cstdlib>                // atoi / atol
#include <utility>                // std::swap

namespace {

constexpr const char* MOD = "FLC";  // Storage log tag

// Column order in the deck file (Inkpoint layout).
enum Col { COL_FRONT = 0, COL_BACK = 1, COL_REPS = 2, COL_EF = 3, COL_INTERVAL = 4, COL_NEXT = 5, TOTAL_COLS = 6 };

char delimiterForPath(const std::string& path) {
  return (path.size() >= 4 && path.compare(path.size() - 4, 4, ".tsv") == 0) ? '\t' : ',';
}

// Read one '\n'-terminated line (without the newline; trailing '\r' stripped).
// Returns false at EOF with nothing read.
bool readLine(HalFile& f, std::string& out) {
  out.clear();
  char c;
  bool any = false;
  while (f.read(&c, 1) == 1) {
    any = true;
    if (c == '\n') break;
    if (c != '\r') out.push_back(c);
  }
  return any;
}

// Parse a whole line into fields, RFC-4180 quote-aware (a quote only opens a
// field at its start, matching Inkpoint's CsvParser::parseLine). Always emits at
// least one field.
void parseRow(const std::string& line, char delim, std::vector<std::string>& out) {
  out.clear();
  std::string field;
  bool inQuotes = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (inQuotes) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
        else inQuotes = false;
      } else {
        field += c;
      }
    } else if (c == '"' && field.empty()) {
      inQuotes = true;
    } else if (c == delim) {
      out.push_back(std::move(field));
      field.clear();
    } else {
      field += c;
    }
  }
  out.push_back(std::move(field));
}

bool isHeaderRow(const std::vector<std::string>& fields) {
  return fields.size() >= 2 && fields[COL_FRONT] == "Front" && fields[COL_BACK] == "Back";
}

// Quote a field for output if it contains the delimiter, a quote, or a newline.
std::string quoteField(const std::string& field, char delim) {
  bool needs = false;
  for (char c : field) {
    if (c == delim || c == '"' || c == '\n' || c == '\r') { needs = true; break; }
  }
  if (!needs) return field;
  std::string out = "\"";
  for (char c : field) {
    if (c == '"') out += "\"\"";
    else out += c;
  }
  out += '"';
  return out;
}

}  // namespace

bool FlashcardDeck::load(const std::string& path, uint32_t session, uint16_t poolSize) {
  path_ = path;
  cards_.clear();
  dueIdx_.clear();
  duePos_ = reviewed_ = 0;

  HalFile f;
  if (!Storage.openFileForRead(MOD, path_, f)) {
    LOG_ERR(MOD, "Cannot open deck: %s", path_.c_str());
    return false;
  }

  const char delim = delimiterForPath(path_);
  bool sawSchedule = false;  // did any row carry SM-2 columns?
  bool headerSeen = false;

  std::string line;
  std::vector<std::string> fields;
  while (readLine(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    parseRow(line, delim, fields);
    if (fields.size() < 2) continue;
    if (!headerSeen && isHeaderRow(fields)) {  // skip a leading Front/Back header
      headerSeen = true;
      continue;
    }

    Card c;
    c.front = fields[COL_FRONT];
    c.back = fields[COL_BACK];
    if (fields.size() >= TOTAL_COLS) {
      sawSchedule = true;
      c.schedule.repetitions = static_cast<uint16_t>(atoi(fields[COL_REPS].c_str()));
      c.schedule.easinessFactor = static_cast<uint16_t>(atoi(fields[COL_EF].c_str()));
      c.schedule.interval = static_cast<uint32_t>(atol(fields[COL_INTERVAL].c_str()));
      c.schedule.nextReviewSession = static_cast<uint32_t>(atol(fields[COL_NEXT].c_str()));
    }
    cards_.push_back(std::move(c));
  }
  f.close();

  // A plain 2-column deck gains SM-2 columns on first load so future grades have
  // somewhere to persist — same as Inkpoint.
  if (!sawSchedule && !cards_.empty()) {
    LOG_DBG(MOD, "Upgrading %s with SM-2 columns", path_.c_str());
    save();
  }

  buildDueList(session, poolSize);
  LOG_DBG(MOD, "Deck %s: %u cards, %u due (session %u)", path_.c_str(), (unsigned)cards_.size(),
          (unsigned)dueIdx_.size(), (unsigned)session);
  return !cards_.empty();
}

void FlashcardDeck::buildDueList(uint32_t session, uint16_t poolSize) {
  dueIdx_.clear();
  duePos_ = 0;
  for (size_t i = 0; i < cards_.size(); ++i) {
    if (cards_[i].schedule.nextReviewSession <= session) {
      dueIdx_.push_back(i);
      if (poolSize > 0 && dueIdx_.size() >= poolSize) break;
    }
  }

  // Fisher-Yates shuffle with a portable xorshift PRNG (no esp_random dependency,
  // so this also compiles for the host/simulator). Seeded from the session and
  // deck size: stable within a session, varied across sessions.
  uint32_t rng = (session * 2654435761u) ^ (static_cast<uint32_t>(cards_.size()) * 40503u) ^ 0x9e3779b9u;
  rng |= 1u;  // xorshift must not start at 0
  auto nextRand = [&rng]() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
  };
  for (size_t i = dueIdx_.size(); i > 1; --i) {
    const size_t j = nextRand() % i;
    std::swap(dueIdx_[i - 1], dueIdx_[j]);
  }
}

const FlashcardDeck::Card* FlashcardDeck::current() const {
  return duePos_ < dueIdx_.size() ? &cards_[dueIdx_[duePos_]] : nullptr;
}

bool FlashcardDeck::grade(Grade g, uint32_t session, uint16_t learningThreshold) {
  if (duePos_ >= dueIdx_.size()) return false;
  Card& c = cards_[dueIdx_[duePos_]];
  c.schedule = SM2::review(c.schedule, g, session, learningThreshold);
  ++duePos_;
  ++reviewed_;
  save();  // persist the whole file, as Inkpoint does after each grade
  return duePos_ < dueIdx_.size();
}

bool FlashcardDeck::save() const {
  const char delim = delimiterForPath(path_);
  const std::string tmp = path_ + ".tmp";

  HalFile f;
  if (!Storage.openFileForWrite(MOD, tmp, f)) {
    LOG_ERR(MOD, "Cannot open deck temp: %s", tmp.c_str());
    return false;
  }

  auto writeStr = [&f](const std::string& s) { f.write(s.c_str(), s.size()); };

  // Header
  std::string header;
  header += "Front";
  header += delim;
  header += "Back";
  header += delim;
  header += "Repetitions";
  header += delim;
  header += "EasinessFactor";
  header += delim;
  header += "Interval";
  header += delim;
  header += "NextReviewSession";
  header += '\n';
  writeStr(header);

  char nums[48];
  for (const Card& c : cards_) {
    std::string row;
    row += quoteField(c.front, delim);
    row += delim;
    row += quoteField(c.back, delim);
    row += delim;
    std::snprintf(nums, sizeof(nums), "%u%c%u%c%lu%c%lu", (unsigned)c.schedule.repetitions, delim,
                  (unsigned)c.schedule.easinessFactor, delim, (unsigned long)c.schedule.interval, delim,
                  (unsigned long)c.schedule.nextReviewSession);
    row += nums;
    row += '\n';
    writeStr(row);
  }
  f.flush();
  f.close();

  Storage.remove(path_.c_str());  // SdFat rename won't overwrite; drop first
  if (!Storage.rename(tmp.c_str(), path_.c_str())) {
    LOG_ERR(MOD, "Cannot rename deck into place: %s", path_.c_str());
    return false;
  }
  return true;
}

size_t FlashcardDeck::countCards(const std::string& path) {
  HalFile f;
  if (!Storage.openFileForRead(MOD, path, f)) return 0;
  const char delim = delimiterForPath(path);
  size_t n = 0;
  bool headerSeen = false;
  std::string line;
  std::vector<std::string> fields;
  while (readLine(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    parseRow(line, delim, fields);
    if (fields.size() < 2) continue;
    if (!headerSeen && isHeaderRow(fields)) { headerSeen = true; continue; }
    ++n;
  }
  f.close();
  return n;
}

size_t FlashcardDeck::countDue(const std::string& path, uint32_t session) {
  HalFile f;
  if (!Storage.openFileForRead(MOD, path, f)) return 0;
  const char delim = delimiterForPath(path);
  size_t n = 0;
  bool headerSeen = false;
  std::string line;
  std::vector<std::string> fields;
  while (readLine(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    parseRow(line, delim, fields);
    if (fields.size() < 2) continue;
    if (!headerSeen && isHeaderRow(fields)) { headerSeen = true; continue; }
    // New cards (no schedule column, or nextReviewSession 0) are always due.
    const uint32_t next = fields.size() >= TOTAL_COLS ? static_cast<uint32_t>(atol(fields[COL_NEXT].c_str())) : 0;
    if (next <= session) ++n;
  }
  f.close();
  return n;
}
