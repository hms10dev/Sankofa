// src/activities/flashcards/FlashcardReviewActivity.h
//
// The review screen: shows a card front, flips on Confirm, grades on Left/Right
// (Again/Good), applies SM-2 + persists, advances. Opened from Home (via a new
// HomeMenuItem::FLASHCARDS) with a deck path, or launched directly on a deck.
//
// Mirrors DictionaryDefinitionActivity's structure: loop() reads input and
// calls finish()/requestUpdate(); render(RenderLock&&) composes the frame and
// ends with GUI.drawButtonHints + displayBuffer().
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"
#include "flashcards/FlashcardDeck.h"

class FlashcardReviewActivity final : public Activity {
 public:
  explicit FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath)
      : Activity("FlashcardReview", renderer, mappedInput), deckPath_(std::move(deckPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { Empty, Front, Back, Done };

  FlashcardDeck deck_;
  std::string deckPath_;
  State state_ = State::Empty;
  uint32_t today_ = 0;  // epoch-day, captured on entry

  void afterGrade();
  std::string deckName() const;  // basename for the header

  // ---- Today's date, from the device RTC ---------------------------------
  // Epoch-day = days since 1970-01-01. Returns 0 when the RTC is absent or
  // never set (oscillator-stopped) — in that degraded mode every card reads as
  // due, so review still works, it just can't space cards out until the clock
  // is set. (Flowe gets time from the phone; CrossPoint can set it via the web
  // settings UI.)
  static uint32_t todayEpochDay();
  // Pure calendar math (Howard Hinnant's days_from_civil) — no <ctime>, exact.
  static long daysFromCivil(int y, unsigned m, unsigned d);

  // ---- Word wrap ----------------------------------------------------------
  // Greedy word-wrap `text` to `maxWidth` px at `fontId`, returning display
  // lines. Adapted from DictionaryDefinitionActivity's wrap (simplified for the
  // short strings on a card face).
  std::vector<std::string> wrapToWidth(int fontId, const std::string& text, int maxWidth) const;
  // Draw wrapped text as a centered block whose vertical center is `centerY`.
  void drawWrappedCentered(int fontId, const std::string& text, int centerY, int maxWidth, bool bold) const;
};
