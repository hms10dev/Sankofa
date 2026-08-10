// src/flashcards/Sm2.h
// Pure SM-2 spaced-repetition scheduling. No I/O, no Arduino deps — compiles on
// host so it can be unit-tested off-device (see test/test_sm2.cpp). Ease is
// stored as EF*1000 (integer) to keep the on-SD sidecar fixed-width and avoid
// float drift between writes.
#pragma once
#include <cstdint>

// Hard ceiling on the scheduled interval. Two reasons:
//   1. intervalDays is uint16_t; an unbroken "Good" streak grows the interval
//      geometrically and WILL overflow 65535 on a mature card, wrapping it back
//      to "due tomorrow". (The host test caught this.)
//   2. Anki uses the same 100-year cap; past this, "review scheduling" is moot.
static constexpr uint16_t SM2_MAX_INTERVAL_DAYS = 36500;  // ~100 years

struct SrsState {
  uint16_t intervalDays = 0;  // last scheduled interval
  uint16_t reps = 0;          // consecutive successful reps
  uint16_t ease = 2500;       // EF * 1000, SM-2 default 2.5, floor 1.3
};

// Apply one review grade. quality (SM-2 0..5):
//   2-button UI maps  Again = 1,  Good = 4.
//   (4-button later:  Again=1, Hard=2, Good=4, Easy=5 — same function.)
// Mutates `s` and returns the next interval in DAYS. Caller sets
//   due = todayEpochDay + returnedInterval.
inline uint16_t sm2Advance(SrsState& s, int quality) {
  uint32_t next;  // widen during compute; clamped to uint16_t on return
  if (quality < 3) {           // lapse: reset the rep chain, see it again tomorrow
    s.reps = 0;
    next = 1;
  } else {
    if (s.reps == 0) {
      next = 1;
    } else if (s.reps == 1) {
      next = 6;
    } else {
      next = static_cast<uint32_t>(s.intervalDays * (s.ease / 1000.0f) + 0.5f);
    }
    s.reps = static_cast<uint16_t>(s.reps + 1);
  }

  // EF update runs on every grade (SM-2). Floor at 1.3.
  float ef = s.ease / 1000.0f;
  ef = ef + (0.1f - (5 - quality) * (0.08f + (5 - quality) * 0.02f));
  if (ef < 1.3f) ef = 1.3f;
  s.ease = static_cast<uint16_t>(ef * 1000.0f + 0.5f);

  if (next > SM2_MAX_INTERVAL_DAYS) next = SM2_MAX_INTERVAL_DAYS;
  s.intervalDays = static_cast<uint16_t>(next);
  return s.intervalDays;
}
