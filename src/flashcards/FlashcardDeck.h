// src/flashcards/FlashcardDeck.h
//
// A flashcard deck backed by a .tsv/.csv file whose SM-2 schedule lives IN the
// file (columns: Front, Back, Repetitions, EasinessFactor, Interval,
// NextReviewSession) — matching the Inkpoint reference, so progress round-trips
// between the two firmwares. Scheduling is session-based (see Sm2.h /
// FlashcardSession.h): a card is due when its NextReviewSession <= the current
// global session. On each grade the whole file is rewritten (crash-safe temp +
// rename), the same way Inkpoint persists.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Sm2.h"

class FlashcardDeck {
 public:
  static constexpr uint16_t DEFAULT_POOL = 20;  // due-pool cap (0 = unlimited)

  struct Card {
    std::string front;
    std::string back;
    CardSchedule schedule;
  };

  // Parse the deck file, read in-file SM-2 columns (adding+saving them if the
  // file is a plain 2-column deck), and build the shuffled due list for
  // `session` — cards with nextReviewSession <= session, capped at `poolSize`
  // (0 = unlimited). Returns false if the file can't be opened or has no cards.
  bool load(const std::string& path, uint32_t session, uint16_t poolSize = DEFAULT_POOL);

  const Card* current() const;  // current due card, or nullptr when the cycle is done

  // Apply a grade to the current card, rewrite the deck file, and advance.
  // Returns true if more due cards remain in this cycle.
  bool grade(Grade g, uint32_t session, uint16_t learningThreshold = SM2::LEARNING_REPS);

  size_t totalCards() const { return cards_.size(); }
  size_t dueCount() const { return duePos_ < dueIdx_.size() ? dueIdx_.size() - duePos_ : 0; }
  size_t reviewedThisCycle() const { return reviewed_; }

  // Lightweight streaming counts for the deck picker (no full load / no sidecar).
  static size_t countCards(const std::string& path);
  static size_t countDue(const std::string& path, uint32_t session);

 private:
  void buildDueList(uint32_t session, uint16_t poolSize);
  bool save() const;

  std::string path_;
  std::vector<Card> cards_;
  std::vector<size_t> dueIdx_;
  size_t duePos_ = 0;
  size_t reviewed_ = 0;
};
