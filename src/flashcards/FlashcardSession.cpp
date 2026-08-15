// src/flashcards/FlashcardSession.cpp
#include "FlashcardSession.h"

#include <HalStorage.h>
#include <Logging.h>

namespace {
constexpr const char* MOD = "FLC";
constexpr const char* DIR = "/.flashcards";
constexpr const char* SESSION_PATH = "/.flashcards/global.session";
}  // namespace

FlashcardSession& FlashcardSession::instance() {
  static FlashcardSession inst;
  return inst;
}

void FlashcardSession::ensureLoaded() {
  if (loaded_) return;
  loaded_ = true;  // load once per boot; the reviewed tally then persists across visits
  cardsReviewed_ = 0;

  HalFile f;
  if (!Storage.exists(SESSION_PATH) || !Storage.openFileForRead(MOD, SESSION_PATH, f)) {
    globalSession_ = 0;
    return;
  }
  uint8_t buf[4] = {0, 0, 0, 0};
  if (f.read(buf, sizeof(buf)) == static_cast<int>(sizeof(buf))) {
    globalSession_ = static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
                     (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
  } else {
    globalSession_ = 0;
  }
  f.close();
  LOG_DBG(MOD, "Loaded global session: %u", (unsigned)globalSession_);
}

void FlashcardSession::save() {
  if (!Storage.exists(DIR)) Storage.mkdir(DIR);
  HalFile f;
  if (!Storage.openFileForWrite(MOD, SESSION_PATH, f)) {
    LOG_ERR(MOD, "Cannot write session file");
    return;
  }
  const uint32_t v = globalSession_;
  const uint8_t buf[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                          static_cast<uint8_t>(v >> 24)};
  f.write(buf, sizeof(buf));
  f.flush();
  f.close();
}

uint32_t FlashcardSession::session() {
  ensureLoaded();
  return globalSession_;
}

void FlashcardSession::onCardReviewed() {
  ensureLoaded();
  cardsReviewed_++;
}

void FlashcardSession::onCycleComplete() {
  ensureLoaded();
  if (bumpedThisRun_) return;

  bool shouldBump = cardsReviewed_ >= DAILY_GOAL;
  // A deck with fewer due cards than the goal counts when all of them are done.
  if (!shouldBump && totalDue_ > 0 && totalDue_ < DAILY_GOAL) {
    shouldBump = cardsReviewed_ >= totalDue_;
  }
  if (!shouldBump) return;

  globalSession_++;
  cardsReviewed_ = 0;
  totalDue_ = 0;
  bumpedThisRun_ = true;
  save();
  LOG_DBG(MOD, "Session bumped to %u (goal met)", (unsigned)globalSession_);
}
