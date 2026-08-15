// src/flashcards/Sm2.h
// Session-based SM-2 scheduling, ported to match the Inkpoint reference so decks
// round-trip between the two firmwares. No wall clock: intervals are counted in
// review *sessions*, and a card's schedule lives in the deck file itself
// (columns Repetitions, EasinessFactor, Interval, NextReviewSession). Ease is
// stored as EF*1000 (integer) so the on-SD columns stay exact.
//
// Pure logic, no I/O or Arduino deps — host-testable (see test/test_sm2.cpp).
#pragma once

#include <cstdint>

struct CardSchedule {
  uint16_t repetitions = 0;
  uint16_t easinessFactor = 2500;    // EF * 1000 (2500 = 2.5), floor 1300
  uint32_t interval = 0;             // interval in sessions
  uint32_t nextReviewSession = 0;    // session number when the card is next due
};

enum class Grade : uint8_t {
  Again = 0,
  Hard = 1,
  Good = 2,
  Easy = 3,
};

namespace SM2 {

// Successful (Good/Easy) reviews before graduating from the fixed learning phase
// to EF-driven growth. Pass 0 to skip the learning phase (pure SM-2).
constexpr uint16_t LEARNING_REPS = 3;

// Apply one grade and return the updated schedule. Intervals are in sessions.
// learningThreshold: cards with repetitions < this use fixed learning intervals.
CardSchedule review(const CardSchedule& card, Grade grade, uint32_t currentSession,
                    uint16_t learningThreshold = LEARNING_REPS);

}  // namespace SM2
