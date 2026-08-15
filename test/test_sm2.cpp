// test/test_sm2.cpp — host unit test for src/flashcards/Sm2.{h,cpp}
// Session-based SM-2, matching the Inkpoint reference.
// Build: g++ -std=c++17 -I../src/flashcards test_sm2.cpp ../src/flashcards/Sm2.cpp -o test_sm2
#include <cstdio>

#include "Sm2.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(c, m) do { if (c) {++g_pass; printf("  PASS  %s\n", m);} else {++g_fail; printf("  FAIL  %s\n", m);} } while (0)
#define CHECK_EQ(g, w, m) do { long _g=(long)(g), _w=(long)(w); if(_g==_w){++g_pass;printf("  PASS  %s (=%ld)\n",m,_g);} else {++g_fail;printf("  FAIL  %s: got %ld want %ld\n",m,_g,_w);} } while (0)

int main() {
  using SM2::review;

  printf("== Learning phase (Good), default threshold 3 ==\n");
  { CardSchedule s;
    s = review(s, Grade::Good, 0); CHECK_EQ(s.interval, 1, "Good #1 -> 1"); CHECK_EQ(s.repetitions, 1, "reps 1");
    CHECK_EQ(s.easinessFactor, 2500, "Good keeps EF");
    s = review(s, Grade::Good, 0); CHECK_EQ(s.interval, 1, "Good #2 -> 1");
    s = review(s, Grade::Good, 0); CHECK_EQ(s.interval, 1, "Good #3 -> 1"); CHECK_EQ(s.repetitions, 3, "reps 3");
    s = review(s, Grade::Good, 0); CHECK_EQ(s.interval, 2, "Good #4 (graduated) -> 2");
    s = review(s, Grade::Good, 0); CHECK_EQ(s.interval, 5, "Good #5 -> 5"); }

  printf("\n== Again resets, EF -200, re-queues same session ==\n");
  { CardSchedule s; s = review(s, Grade::Good, 0); s = review(s, Grade::Good, 0);
    s = review(s, Grade::Again, 7);
    CHECK_EQ(s.repetitions, 0, "reps reset"); CHECK_EQ(s.interval, 0, "interval 0");
    CHECK_EQ(s.nextReviewSession, 7, "due same session"); CHECK_EQ(s.easinessFactor, 2300, "EF -200"); }

  printf("\n== Hard: keeps reps, EF -50, learning interval 1 ==\n");
  { CardSchedule s; s = review(s, Grade::Hard, 3);
    CHECK_EQ(s.repetitions, 0, "Hard does not advance reps"); CHECK_EQ(s.easinessFactor, 2450, "EF -50");
    CHECK_EQ(s.interval, 1, "Hard learning -> 1"); CHECK_EQ(s.nextReviewSession, 4, "next = 3 + 1"); }

  printf("\n== Hard in SM-2 phase: 70%% of interval ==\n");
  { CardSchedule s; for (int k = 0; k < 5; k++) s = review(s, Grade::Good, 0);  // interval grows to 5
    s = review(s, Grade::Hard, 0); CHECK_EQ(s.interval, 3, "Hard -> 5*7/10 = 3"); }

  printf("\n== Easy from fresh: EF +100, interval 2 ==\n");
  { CardSchedule s; s = review(s, Grade::Easy, 0);
    CHECK_EQ(s.easinessFactor, 2600, "Easy +100"); CHECK_EQ(s.repetitions, 1, "reps 1"); CHECK_EQ(s.interval, 2, "Easy -> 2"); }

  printf("\n== EF floor 1300 ==\n");
  { CardSchedule s; for (int k = 0; k < 10; k++) s = review(s, Grade::Again, 0);
    CHECK_EQ(s.easinessFactor, 1300, "EF floors at 1300"); }

  printf("\n== nextReviewSession = currentSession + interval ==\n");
  { CardSchedule s; s = review(s, Grade::Good, 100); CHECK_EQ(s.nextReviewSession, 101, "100 + 1"); }

  printf("\n== threshold 0 (unlimited pool): pure SM-2, no learning phase ==\n");
  { CardSchedule s; s = review(s, Grade::Good, 0, 0); CHECK_EQ(s.interval, 2, "Good #1 (no learning) -> 2"); }

  printf("\n----------------------------------------\n");
  printf("SM-2 host test: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
