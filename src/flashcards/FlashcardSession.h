// src/flashcards/FlashcardSession.h
// Global review-session counter, ported from Inkpoint's AnkiSessionManager.
// Session-based SM-2 has no wall clock: a card due at "session N" becomes due
// once the global session counter reaches N. The counter advances by one when a
// review cycle completes and the daily goal was met, and is capped to a single
// advance per boot so leaving and re-entering review can't skip days.
//
// Persisted as a little-endian uint32 at /.flashcards/global.session. The
// per-session "cards reviewed" tally is intentionally NOT persisted — it resets
// on every boot, so the goal must be reached in one sitting.
#pragma once

#include <cstdint>

class FlashcardSession {
 public:
  // Cards graded in one sitting before the session advances (Inkpoint default).
  static constexpr uint16_t DAILY_GOAL = 20;

  static FlashcardSession& instance();

  uint32_t session();                  // current global session (lazy-loads once)
  uint16_t cardsReviewed() const { return cardsReviewed_; }

  void setTotalDue(uint16_t n) { totalDue_ = n; }
  void onCardReviewed();               // count a grade (no bump)
  void onCycleComplete();              // bump the session once if the goal was met

 private:
  FlashcardSession() = default;
  void ensureLoaded();
  void save();

  uint32_t globalSession_ = 0;
  uint16_t cardsReviewed_ = 0;
  uint16_t totalDue_ = 0;
  bool loaded_ = false;
  bool bumpedThisRun_ = false;
};
