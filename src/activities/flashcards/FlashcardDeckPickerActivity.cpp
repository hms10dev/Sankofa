// src/activities/flashcards/FlashcardDeckPickerActivity.cpp
#include "FlashcardDeckPickerActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>

#include "FlashcardReviewActivity.h"
#include "components/UITheme.h"  // GUI singleton (drawButtonHints)
#include "flashcards/FlashcardDeck.h"
#include "flashcards/FlashcardSession.h"
#include "fontIds.h"

namespace {
constexpr int TITLE_FONT = UI_12_FONT_ID;
constexpr int ROW_FONT = UI_12_FONT_ID;
constexpr int META_FONT = UI_10_FONT_ID;
constexpr int SIDE_PADDING = 20;
constexpr int LIST_START_Y = 54;
constexpr int ROW_HEIGHT = 44;

bool isDeckFile(const std::string& name) {
  return name.size() > 4 &&
         (name.compare(name.size() - 4, 4, ".tsv") == 0 || name.compare(name.size() - 4, 4, ".csv") == 0);
}

std::string titleOf(const std::string& name) {
  const auto dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}
}  // namespace

void FlashcardDeckPickerActivity::onEnter() {
  Activity::onEnter();
  scanDecks();
  requestUpdate();
}

void FlashcardDeckPickerActivity::scanDecks() {
  decks_.clear();
  const uint32_t session = FlashcardSession::instance().session();
  for (const String& f : Storage.listFiles("/flashcards", 100)) {
    const std::string name(f.c_str());
    if (!isDeckFile(name)) continue;
    DeckEntry e;
    e.title = titleOf(name);
    e.path = "/flashcards/" + name;
    e.cards = static_cast<uint16_t>(FlashcardDeck::countCards(e.path));
    e.due = static_cast<uint16_t>(FlashcardDeck::countDue(e.path, session));
    decks_.push_back(std::move(e));
  }
  std::sort(decks_.begin(), decks_.end(),
            [](const DeckEntry& a, const DeckEntry& b) { return a.title < b.title; });
  if (selected_ >= static_cast<int>(decks_.size())) {
    selected_ = decks_.empty() ? 0 : static_cast<int>(decks_.size()) - 1;
  }
}

int FlashcardDeckPickerActivity::pageItems() const {
  const int available = renderer.getScreenHeight() - LIST_START_Y - ROW_HEIGHT;
  return std::max(1, available / ROW_HEIGHT);
}

void FlashcardDeckPickerActivity::openSelected() {
  if (decks_.empty()) return;
  const std::string path = decks_[selected_].path;
  // Re-scan on return so due/total counts refresh after a review session.
  // requestUpdate() is invoked automatically once the result handler returns.
  startActivityForResult(std::make_unique<FlashcardReviewActivity>(renderer, mappedInput, path),
                         [this](const ActivityResult&) { scanDecks(); });
}

void FlashcardDeckPickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  const int total = static_cast<int>(decks_.size());
  if (total == 0) return;

  const int page = pageItems();
  buttonNavigator.onNextRelease([this, total] {
    selected_ = ButtonNavigator::nextIndex(selected_, total);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, total] {
    selected_ = ButtonNavigator::previousIndex(selected_, total);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, total, page] {
    selected_ = ButtonNavigator::nextPageIndex(selected_, total, page);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, total, page] {
    selected_ = ButtonNavigator::previousPageIndex(selected_, total, page);
    requestUpdate();
  });
}

void FlashcardDeckPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();

  renderer.drawText(TITLE_FONT, SIDE_PADDING, 22, "Flashcards", true, EpdFontFamily::BOLD);

  if (decks_.empty()) {
    renderer.drawCenteredText(ROW_FONT, h / 2 - renderer.getLineHeight(ROW_FONT), "No decks found", true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(META_FONT, h / 2 + 8, "Put .tsv or .csv decks in /flashcards on the SD card", true,
                              EpdFontFamily::REGULAR);
    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int total = static_cast<int>(decks_.size());
  const int page = pageItems();
  const int pageStart = (selected_ / page) * page;

  for (int i = 0; i < page; ++i) {
    const int idx = pageStart + i;
    if (idx >= total) break;

    const int rowY = LIST_START_Y + i * ROW_HEIGHT;
    const bool sel = (idx == selected_);
    if (sel) renderer.fillRect(0, rowY, w - 1, ROW_HEIGHT, true);

    const DeckEntry& d = decks_[idx];
    char meta[28];
    if (d.due > 0) {
      std::snprintf(meta, sizeof(meta), "%u due", (unsigned)d.due);
    } else {
      std::snprintf(meta, sizeof(meta), "%u card%s", (unsigned)d.cards, d.cards == 1 ? "" : "s");
    }
    const int metaW = renderer.getTextWidth(META_FONT, meta);

    const std::string title = renderer.truncatedText(ROW_FONT, d.title.c_str(), w - 2 * SIDE_PADDING - metaW - 12);
    renderer.drawText(ROW_FONT, SIDE_PADDING, rowY + 10, title.c_str(), !sel, EpdFontFamily::BOLD);
    renderer.drawText(META_FONT, w - SIDE_PADDING - metaW, rowY + 13, meta, !sel);
  }

  // Left button steps up the list, Right steps down (ButtonNavigator prev/next).
  const auto labels = mappedInput.mapLabels("Back", "Open", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
