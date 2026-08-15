// src/activities/flashcards/FlashcardReviewActivity.cpp
#include "FlashcardReviewActivity.h"

#include <GfxRenderer.h>

#include <cstdio>

#include "components/UITheme.h"  // GUI singleton (drawButtonHints)
#include "flashcards/FlashcardSession.h"
#include "fontIds.h"

namespace {
// Card face (serif). BITTER_16 (16px "large"), not BITTER_18 (18px "xlarge"):
// the default firmware build sets -DOMIT_XLARGE_FONT, so an 18px font isn't
// compiled in and would render nothing on device.
constexpr int CARD_FONT = BITTER_16_FONT_ID;
constexpr int HEAD_FONT = UI_12_FONT_ID;      // deck name / counts
constexpr int META_FONT = UI_10_FONT_ID;      // small meta
constexpr int SIDE_PADDING = 20;

// Hold Back this long on the answer screen to exit the review (a short Back tap
// grades Again, since all four buttons rate the card).
constexpr unsigned long REVIEW_EXIT_HOLD_MS = 700;
}  // namespace

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
      if (c == '\n') {
        lines.push_back(line);
        line.clear();
      }
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
  session_ = FlashcardSession::instance().session();

  if (!deck_.load(deckPath_, session_) || deck_.dueCount() == 0) {
    state_ = State::Empty;
  } else {
    state_ = State::Front;
  }
  FlashcardSession::instance().setTotalDue(static_cast<uint16_t>(deck_.dueCount()));
  requestUpdate();
}

void FlashcardReviewActivity::onExit() {
  // Leaving the review completes the cycle: if the daily goal was met, the
  // global session advances (which is what spaces cards out to future sessions).
  FlashcardSession::instance().onCycleComplete();
  Activity::onExit();
}

void FlashcardReviewActivity::gradeAndAdvance(Grade g) {
  deck_.grade(g, session_);
  FlashcardSession::instance().onCardReviewed();
  afterGrade();
}

void FlashcardReviewActivity::afterGrade() {
  state_ = deck_.current() ? State::Front : State::Done;
  requestUpdate();
}

void FlashcardReviewActivity::loop() {
  switch (state_) {
    case State::Front:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        finish();
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        state_ = State::Back;  // flip
        requestUpdate();
      }
      break;

    case State::Back:
      // 4-button grading with the answer shown: Confirm/Left/Right rate on press
      // (Hard/Good/Easy). Back does double duty -- a short tap grades Again,
      // holding it ~0.7s exits the review (no spare button for a dedicated Back).
      if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
        if (!backHoldHandled_ && mappedInput.getHeldTime() >= REVIEW_EXIT_HOLD_MS) {
          backHoldHandled_ = true;  // long press -> exit
          finish();
          return;
        }
      } else {
        if (!backHoldHandled_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
          gradeAndAdvance(Grade::Again);  // short tap -> Again
          break;
        }
        backHoldHandled_ = false;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        gradeAndAdvance(Grade::Hard);
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        gradeAndAdvance(Grade::Good);
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        gradeAndAdvance(Grade::Easy);
      }
      break;

    case State::Empty:
    case State::Done:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
          mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        finish();
      }
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
      renderer.drawCenteredText(META_FONT, h / 2 + 8, "Put .tsv or .csv decks in /flashcards on the SD card", true,
                                EpdFontFamily::REGULAR);
      break;

    case State::Done: {
      char msg[48];
      std::snprintf(msg, sizeof(msg), "%u reviewed", (unsigned)deck_.reviewedThisCycle());
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
    case State::Back:  labels = mappedInput.mapLabels("Again", "Hard", "Good", "Easy"); break;
    default:           labels = mappedInput.mapLabels("Back", "Done", "", ""); break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
