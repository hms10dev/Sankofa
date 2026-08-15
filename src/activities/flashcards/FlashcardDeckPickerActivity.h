// src/activities/flashcards/FlashcardDeckPickerActivity.h
//
// On-device deck picker: lists the .tsv/.csv decks in /flashcards and opens the
// selected one in FlashcardReviewActivity. Reached from Home -> Flashcards, so
// a device with several decks is no longer stuck on whichever sorts first.
//
// Layout mirrors EpubReaderBookmarkListActivity (paged list + highlight bar +
// ButtonNavigator) and the sibling FlashcardReviewActivity (full-screen frame,
// GUI.drawButtonHints, displayBuffer()).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FlashcardDeckPickerActivity final : public Activity {
 public:
  explicit FlashcardDeckPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardDeckPicker", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct DeckEntry {
    std::string title;  // basename without extension (for display)
    std::string path;   // full "/flashcards/<file>"
    uint16_t cards;     // total card count
    uint16_t due;       // cards due at the current session
  };

  std::vector<DeckEntry> decks_;
  int selected_ = 0;
  ButtonNavigator buttonNavigator;

  void scanDecks();
  int pageItems() const;
  void openSelected();
};
