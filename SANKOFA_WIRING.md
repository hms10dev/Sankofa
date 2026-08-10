# Sankofa — wiring spec (flashcards → CrossPoint fork)

Applies the scaffold in `src/flashcards/` + `src/activities/flashcards/` into a
CrossPoint fork named **Sankofa**. Four touch points, all traced from real
`main`. Keep these edits in ONE commit, separate from the new files — it's your
only rebase-conflict surface.

> Note: `tr(STR_…)` calls below are localized strings. For a first build you may
> use a plain literal (e.g. `"Flashcards"`) and add a real `STR_FLASHCARDS`
> later. Same for the launcher icon: reuse an existing `UIIcon` first, draw a
> proper one afterward.

---

## 1. `src/activities/ActivityManager.h`

Add the menu item to the enum (append — keep existing values stable):
```cpp
enum class HomeMenuItem { NONE, FILE_BROWSER, RECENTS, OPDS_BROWSER,
                          FILE_TRANSFER, SETTINGS_MENU, FLASHCARDS /* NEW */ };
```
Declare the launcher next to the other `goTo…`:
```cpp
void goToFlashcards(std::string deckPath);
```

## 2. `src/activities/ActivityManager.cpp`

Include the activity header (near the other activity includes):
```cpp
#include "activities/flashcards/FlashcardReviewActivity.h"
```
Implement, mirroring `goToFileTransfer`:
```cpp
void ActivityManager::goToFlashcards(std::string deckPath) {
  replaceActivity(std::make_unique<FlashcardReviewActivity>(renderer, mappedInput, std::move(deckPath)));
}
```

## 3. `src/activities/home/HomeActivity.h`

`FLASHCARDS` must appear consistently in the two index mappers and the count.
Place it just before Settings so Settings stays visually last.

`getMenuItemCount()` base 4 → 5:
```cpp
int count = 5;  // File Browser, Recents, File transfer, Flashcards, Settings
```
`menuItemToIndex(...)` — add before the SETTINGS line:
```cpp
  if (item == HomeMenuItem::FILE_TRANSFER) return i; ++i;
  if (item == HomeMenuItem::FLASHCARDS) return i; ++i;      // NEW
  if (item == HomeMenuItem::SETTINGS_MENU) return i;
```
`indexToMenuItem(...)` — add before the SETTINGS line:
```cpp
  if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
  if (idx == i++) return HomeMenuItem::FLASHCARDS;          // NEW
  if (idx == i)   return HomeMenuItem::SETTINGS_MENU;
```
Declare the handler (next to `onFileTransferOpen`):
```cpp
void onFlashcardsOpen();
```

## 4. `src/activities/home/HomeActivity.cpp`

**loop() dispatch** — add a case in the `switch (indexToMenuItem(...))`:
```cpp
    case HomeMenuItem::FLASHCARDS:
      onFlashcardsOpen();
      break;
```
**render() menu lists** — insert Flashcards before Settings in BOTH vectors so
they stay index-aligned with the mappers:
```cpp
std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS),
                                      tr(STR_FILE_TRANSFER), "Flashcards", tr(STR_SETTINGS_TITLE)};
std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Recent /*TODO real icon*/, Settings};
```
**Implement the handler** (near `onFileTransferOpen`). MVP opens the first `.tsv`
in `/flashcards`; a deck picker is later polish:
```cpp
void HomeActivity::onFlashcardsOpen() {
  std::string deck;
  for (const String& f : Storage.listFiles("/flashcards", 50)) {
    std::string name(f.c_str());
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tsv") == 0) {
      deck = "/flashcards/" + name;   // VERIFY: does listFiles return basenames or full paths? adjust if needed
      break;
    }
  }
  if (deck.empty()) deck = "/flashcards/deck.tsv";  // missing file → review screen shows its empty state
  activityManager.goToFlashcards(deck);
}
```
`<HalStorage.h>` is already included in HomeActivity.cpp.

---

## 5. Sankofa branding (light)

- Name the GitHub repo **`sankofa`** (fork of `crosspoint-reader`).
- Optional: add a "Sankofa" label to the About screen (`AboutActivity`) and/or a
  fork tag in the version string. The version is `[crosspoint] version` in
  `platformio.ini` + `CROSSPOINT_VERSION` from `scripts/git_branch.py`. Purely
  cosmetic — skip for first light.

## 6. Guardrails

- Do **not** touch `partitions.csv`, the bootloader, `board_upload` offsets, or
  the OTA path.
- Device is confirmed **not USB-locked**, so a bad build = a 2-minute reflash.
- After build is green, put a sample deck on the SD card:
  `/flashcards/spanish.tsv` with real TAB characters:
  ```
  hola	hello
  gracias	thank you
  ```

## 7. Verify order

1. Host tests (no hardware): `cd test && g++ -std=c++17 -I../src/flashcards test_sm2.cpp -o t && ./t && g++ -std=c++17 test_helpers.cpp -o th && ./th`
2. Build: `pio run -e default` — fix any API mismatches against the real headers.
3. Flash: `pio run -e default -t upload`
4. Monitor: `pio device monitor -b 115200`
5. On device: Home → Flashcards → review the sample deck → grade a card, sleep,
   wake, confirm it's no longer due (proves `.srs` persistence).
