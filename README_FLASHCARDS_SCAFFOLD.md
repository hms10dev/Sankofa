# Flashcards scaffold for your CrossPoint fork

Real, in-tree-ready source for the flashcards feature. Drop these into your fork,
wire the four touch points below, build, flash. Written against CrossPoint's
*actual* APIs (traced from current `main`), not pseudocode.

## What's here

```
src/flashcards/Sm2.h                 SM-2 scheduling (pure, host-tested, interval-capped)
src/flashcards/FlashcardDeck.h/.cpp  Load .tsv + .srs sidecar, due queue, grade+persist, appendCard
src/activities/flashcards/
  FlashcardReviewActivity.h/.cpp      The review screen (Front → Flip → Again/Good → Done)
test/test_sm2.cpp                    Host unit test for Sm2.h
```

## Verified vs. compiles-in-tree

- **`Sm2.h` — proven.** `test/test_sm2.cpp` passes 14/14 on host, including the
  interval-overflow cap (a `uint16_t` interval would otherwise wrap a mature card
  back to "due tomorrow"; now clamped at `SM2_MAX_INTERVAL_DAYS` = 36500).
  Run: `cd test && g++ -std=c++17 -I../src/flashcards -Wall -Wextra test_sm2.cpp -o t && ./t`
- **`FlashcardDeck.cpp` — syntax-clean.** Compiled `-fsyntax-only` against stub
  headers matching CrossPoint's real `HalStorage`/`HalFile` signatures. Full
  compile happens in your tree (it needs the freeink-sdk headers).
- **`FlashcardReviewActivity.cpp`** — written against the real `Activity` /
  `GfxRenderer` / `MappedInputManager` / `GUI` APIs (mirrors
  `DictionaryDefinitionActivity`), compiles in-tree.

## The four integration touch points

Keep these in ONE small commit, separate from the new files — it's your only
rebase-conflict surface (see the design doc §11).

1. **`src/activities/ActivityManager.h`** — add the enum value + a launcher:
   ```cpp
   enum class HomeMenuItem { NONE, FILE_BROWSER, RECENTS, OPDS_BROWSER,
                             FILE_TRANSFER, SETTINGS_MENU, FLASHCARDS /* NEW */ };
   // ...
   void goToFlashcards(std::string deckPath);   // implement in ActivityManager.cpp:
   //   replaceActivity(std::make_unique<FlashcardReviewActivity>(renderer, mappedInput, std::move(deckPath)));
   ```

2. **`src/activities/home/HomeActivity.{h,cpp}`** — add `FLASHCARDS` to
   `menuItemToIndex` / `indexToMenuItem`, `+1` in `getMenuItemCount()`, add an
   `onFlashcardsOpen()` that calls `activityManager.goToFlashcards(...)`, and a
   menu row + icon in `render()`. Same 5-lines-per-spot pattern every menu item
   already follows.

3. **Deck picking.** Simplest: reuse `FileBrowserActivity` filtered to
   `/flashcards`, and hand the chosen path to `goToFlashcards()`. (A dedicated
   deck-list Activity is optional polish.)

4. **Dictionary → deck hook (the differentiator).** In
   `src/activities/reader/DictionaryDefinitionActivity`, add a "Save to deck"
   button that calls:
   ```cpp
   FlashcardDeck::appendCard("/flashcards/reading.tsv", headword, firstLineOf(definition));
   ```
   `headword` and `definition` are already members — ~15 lines total.

## Integration seams (marked `TODO` in the code)

- **Today's date.** `FlashcardReviewActivity::todayEpochDay()` uses a
  `std::time` fallback so it compiles on host. On device, source it from the
  freeink-sdk `Rtc` lib (Flowe reads it via `ClockStore`). One function to swap.
- **Word-wrap long card faces.** The scaffold draws a single centered line.
  For long fronts/backs, reuse `DictionaryDefinitionActivity`'s `wrapText()` —
  it's a solved greedy UTF-8 wrapper right next door.
- **On-device add/edit.** `KeyboardEntryActivity` returns a `KeyboardResult`;
  call it twice (front, back) via `startActivityForResult` and feed
  `FlashcardDeck::appendCard`.

## Deck format (so you can test immediately)

`/flashcards/spanish.tsv` — one card per line, a literal TAB between front/back:

```
hola	hello
gracias	thank you
por favor	please
```

`#`-lines and blank lines are skipped. Scheduling state is written automatically
to `/flashcards/spanish.srs` (binary, crash-safe temp-then-rename). This is the
Anki/Quizlet export shape, so real decks import by copying the file.

## Suggested build order

1. Drop in files, wire touch points 1–2, build, confirm Flashcards appears on Home.
2. Put a 3-line `.tsv` on the SD; review it end to end (flip, Again/Good, Done).
3. Confirm `.srs` persists across a sleep/wake (grade a card, sleep, wake — it's
   no longer due).
4. Add the dictionary hook (touch point 4) — the payoff feature.
5. Then polish: word-wrap, keyboard add, 4-button grading.
