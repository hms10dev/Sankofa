// src/flashcards/Sm2.cpp
// Ported from the Inkpoint reference (src/anki/SM2.cpp) so scheduling is
// bit-for-bit consistent between the two firmwares.
#include "Sm2.h"

#include <algorithm>

namespace SM2 {

CardSchedule review(const CardSchedule& card, Grade grade, uint32_t currentSession, uint16_t learningThreshold) {
  CardSchedule next = card;

  // Easiness factor, in thousandths: Again -200, Hard -50, Good 0, Easy +100.
  switch (grade) {
    case Grade::Again:
      next.easinessFactor = card.easinessFactor >= 1500 ? card.easinessFactor - 200 : 1300;
      break;
    case Grade::Hard:
      next.easinessFactor = card.easinessFactor >= 1350 ? card.easinessFactor - 50 : 1300;
      break;
    case Grade::Good:
      break;
    case Grade::Easy:
      next.easinessFactor = card.easinessFactor + 100;
      break;
  }
  next.easinessFactor = std::max(next.easinessFactor, static_cast<uint16_t>(1300));

  if (grade == Grade::Again) {
    // Reset to the start of the learning phase; re-queued in the same session.
    next.repetitions = 0;
    next.interval = 0;
    next.nextReviewSession = currentSession;
  } else if (grade == Grade::Hard) {
    // Hard never advances repetitions — the card stays in its current stage.
    // Learning phase: back next session. SM-2 phase: 30% interval reduction.
    if (card.repetitions < learningThreshold) {
      next.interval = 1;
    } else {
      next.interval = std::max(static_cast<uint32_t>(1), card.interval * 7 / 10);
    }
    next.nextReviewSession = currentSession + next.interval;
  } else {
    // Good or Easy: advance repetitions.
    if (card.repetitions < learningThreshold && grade != Grade::Easy) {
      next.interval = 1;  // learning phase (Good only)
    } else {
      // SM-2 phase, or Easy at any repetition count. Easy grows from the first
      // review; interval=0 truncates to 2 via the max — one soft check-in first.
      next.interval =
          std::max(static_cast<uint32_t>(2), static_cast<uint32_t>(card.interval * next.easinessFactor / 1000));
      if (grade == Grade::Easy) {
        next.interval = std::max(static_cast<uint32_t>(2), next.interval * 13 / 10);
      }
    }
    next.repetitions = card.repetitions + 1;
    next.nextReviewSession = currentSession + next.interval;
  }

  return next;
}

}  // namespace SM2
