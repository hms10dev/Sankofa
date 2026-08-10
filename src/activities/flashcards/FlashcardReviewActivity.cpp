// src/activities/flashcards/FlashcardReviewActivity.cpp
#include "FlashcardReviewActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>  // halClock singleton (DS3231 wall clock)

#include <cstdio>

#include "components/UITheme.h"  // GUI singleton (drawButtonHints)
#include "fontIds.h"

namespace {
constexpr int CARD_FONT = BITTER_18_FONT_ID;  // card face (serif)
constexpr int HEAD_FONT = UI_12_FONT_ID;         // deck name / counts
constexpr int META_FONT = UI_10_FONT_ID;         // small meta
constexpr int SIDE_PADDING = 20;

// SM-2 quality mapping for the 2-button UI.
constexpr int GRADE_AGAIN = 1;
constexpr int GRADE_GOOD = 4;
}  // namespace

// ---- date ------------------------------------------------------------------

long FlashcardReviewActivity::daysFromCivil(int y, unsigned m, unsigned d) {
  // Days since 1970-01-01, valid for any Gregorian date. Integer-exact.
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);              // [0, 399]
  const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;   // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;             // [0, 146096]
  return static_cast<long>(era) * 146097 + static_cast<long>(doe) - 719468;
}

uint32_t FlashcardReviewActivity::todayEpochDay() {
  // halClock is the shared HAL wall clock (begun at boot). getDateTime() returns
  // the raw RTC date in UTC; for MVP scheduling we treat that as "today". A
  // future refinement could apply SETTINGS.clockUtcOffsetQ so the day rolls over
  // at local midnight (matching HalClock::formatDate).
  uint16_t year;
  uint8_t month, day, hour, minute;
  if (halClock.isAvailable() && halClock.getDateTime(year, month, day, hour, minute)) {
    const long days = daysFromCivil(year, month, day);
    return days > 0 ? static_cast<uint32_t>(days) : 0;
  }
  return 0;  // RTC absent / unset → everything reads as due (see header note)
}

// ---- word wrap -------------------------------------------------------------

std::vector<std::string> FlashcardReviewActivity::wrapToWidth(int fontId, const std::string& text, int maxWidth) const {
  std::vector<std::string> lines;
  std::string line, word;
  auto flushWord = [&]() {
    if (word.empty()) return;
    std::string candidate = line.empty() ? word : line + " " + word;
    if (renderer.getTextWidth(fontId, candidate.c_str()) <= maxWidth || line.empty()) {
      line = std::move(candidate);
    } else {
      lines.push_back(line);
      line = word;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == ' ' || c == '\n') {
      flushWord();
      if (c == '\n') { lines.push_back(line); line.clear(); }
    } else {
      word.push_back(c);
    }
  }
  flushWord();
  if (!line.empty()) lines.push_back(line);
  if (lines.empty()) lines.push_back("");
  return lines;
}

void FlashcardReviewActivity::drawWrappedCentered(int fontId, const std::string& text, int centerY, int maxWidth,
                                                  bool bold) const {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const auto lines = wrapToWidth(fontId, text, maxWidth);
  const int lh = renderer.getLineHeight(fontId);
  int y = centerY - (static_cast<int>(lines.size()) * lh) / 2;
  for (const auto& l : lines) {
    renderer.drawCenteredText(fontId, y, l.c_str(), true, style);
    y += lh;
  }
}

// ---- lifecycle -------------------------------------------------------------

std::string FlashcardReviewActivity::deckName() const {
  auto slash = deckPath_.find_last_of('/');
  std::string base = slash == std::string::npos ? deckPath_ : deckPath_.substr(slash + 1);
  auto dot = base.find_last_of('.');
  return dot == std::string::npos ? base : base.substr(0, dot);
}

void FlashcardReviewActivity::onEnter() {
  Activity::onEnter();
  today_ = todayEpochDay();

  if (!deck_.load(deckPath_, today_) || deck_.dueCount() == 0) {
    state_ = State::Empty;
  } else {
    state_ = State::Front;
  }
  requestUpdate();
}

void FlashcardReviewActivity::afterGrade() {
  state_ = deck_.current() ? State::Front : State::Done;
  requestUpdate();
}

void FlashcardReviewActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  switch (state_) {
    case State::Front:
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        state_ = State::Back;  // flip
        requestUpdate();
      }
      break;

    case State::Back:
      if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        deck_.grade(GRADE_AGAIN, today_);
        afterGrade();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        deck_.grade(GRADE_GOOD, today_);
        afterGrade();
      }
      break;

    case State::Empty:
    case State::Done:
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) finish();
      break;
  }
}

void FlashcardReviewActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int contentW = w - 2 * SIDE_PADDING;

  if (state_ == State::Front || state_ == State::Back) {
    const int headerY = 24;
    renderer.drawText(HEAD_FONT, SIDE_PADDING, headerY, deckName().c_str(), true, EpdFontFamily::BOLD);
    char count[24];
    std::snprintf(count, sizeof(count), "%u due", (unsigned)deck_.dueCount());
    const int cw = renderer.getTextWidth(META_FONT, count);
    renderer.drawText(META_FONT, w - SIDE_PADDING - cw, headerY, count);
  }

  const FlashcardDeck::Card* card = deck_.current();

  switch (state_) {
    case State::Empty:
      renderer.drawCenteredText(HEAD_FONT, h / 2 - renderer.getLineHeight(HEAD_FONT), "No cards due", true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(META_FONT, h / 2 + 8, "Put .tsv decks in /flashcards on the SD card", true,
                                EpdFontFamily::REGULAR);
      break;

    case State::Done: {
      char msg[48];
      std::snprintf(msg, sizeof(msg), "%u reviewed today", (unsigned)deck_.reviewedThisSession());
      renderer.drawCenteredText(HEAD_FONT, h / 2 - renderer.getLineHeight(HEAD_FONT), "All caught up", true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(META_FONT, h / 2 + 8, msg, true, EpdFontFamily::REGULAR);
      break;
    }

    case State::Front:
      if (card) drawWrappedCentered(CARD_FONT, card->front, h / 2, contentW, /*bold=*/false);
      break;

    case State::Back:
      if (card) {
        // Front in the upper third, answer (bold) in the lower — each wrapped.
        drawWrappedCentered(CARD_FONT, card->front, h / 2 - h / 5, contentW, /*bold=*/false);
        renderer.drawCenteredText(META_FONT, h / 2 - renderer.getLineHeight(META_FONT) / 2, "———", true,
                                  EpdFontFamily::REGULAR);
        drawWrappedCentered(CARD_FONT, card->back, h / 2 + h / 5, contentW, /*bold=*/true);
      }
      break;
  }

  MappedInputManager::Labels labels;
  switch (state_) {
    case State::Front: labels = mappedInput.mapLabels("Back", "Flip", "", ""); break;
    case State::Back:  labels = mappedInput.mapLabels("Back", "", "Again", "Good"); break;
    default:           labels = mappedInput.mapLabels("Back", "Done", "", ""); break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
