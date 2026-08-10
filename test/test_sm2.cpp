// test/test_sm2.cpp — host unit test for src/flashcards/Sm2.h
// Build: g++ -std=c++17 -I../src/flashcards -Wall -Wextra test_sm2.cpp -o test_sm2
#include <cstdio>
#include "Sm2.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(c, m) do { if (c) {++g_pass; printf("  PASS  %s\n", m);} else {++g_fail; printf("  FAIL  %s\n", m);} } while (0)
#define CHECK_EQ(g, w, m) do { long _g=(long)(g), _w=(long)(w); if(_g==_w){++g_pass;printf("  PASS  %s (=%ld)\n",m,_g);} else {++g_fail;printf("  FAIL  %s: got %ld want %ld\n",m,_g,_w);} } while (0)

int main() {
  const int Good = 4, Again = 1, Easy = 5;

  printf("== Learning steps (all Good) ==\n");
  { SrsState s;
    CHECK_EQ(sm2Advance(s, Good), 1, "1st Good -> 1d");   CHECK_EQ(s.reps, 1, "reps 1");
    CHECK_EQ(s.ease, 2500, "Good keeps ease");
    CHECK_EQ(sm2Advance(s, Good), 6, "2nd Good -> 6d");
    CHECK_EQ(sm2Advance(s, Good), 15, "3rd Good -> 15d");
    CHECK_EQ(sm2Advance(s, Good), 38, "4th Good -> 38d"); }

  printf("\n== Lapse resets ==\n");
  { SrsState s; sm2Advance(s,Good); sm2Advance(s,Good); sm2Advance(s,Good);
    CHECK_EQ(sm2Advance(s, Again), 1, "Again -> 1d"); CHECK_EQ(s.reps, 0, "reps reset");
    CHECK_EQ(s.ease, 1960, "Again lowers ease to 1960"); }

  printf("\n== Ease floor 1.3 ==\n");
  { SrsState s; for (int k=0;k<8;k++) sm2Advance(s, Again);
    CHECK_EQ(s.ease, 1300, "ease floors at 1300"); }

  printf("\n== Easy raises ease ==\n");
  { SrsState s; sm2Advance(s, Easy); CHECK_EQ(s.ease, 2600, "Easy -> 2600"); }

  printf("\n== Interval cap / no overflow (the bug the first test surfaced) ==\n");
  { SrsState s; uint16_t iv = 0;
    for (int k=0;k<40;k++) iv = sm2Advance(s, Good);   // would blow past uint16_t uncapped
    CHECK_EQ(iv, SM2_MAX_INTERVAL_DAYS, "interval clamps at 36500, never wraps");
    CHECK(s.intervalDays <= SM2_MAX_INTERVAL_DAYS, "stored interval stays within cap");
    // A capped card that keeps being remembered must never suddenly become "due tomorrow".
    bool everWrappedSmall = false;
    SrsState s2; uint16_t prev = 0;
    for (int k=0;k<60;k++){ uint16_t x = sm2Advance(s2, Good); if (k>4 && x < prev) everWrappedSmall = true; prev = x; }
    CHECK(!everWrappedSmall, "interval is monotonic up to the cap (no wraparound)"); }

  printf("\n----------------------------------------\n");
  printf("SM-2 host test: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
