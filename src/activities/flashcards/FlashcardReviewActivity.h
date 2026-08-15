// src/activities/flashcards/FlashcardReviewActivity.h
//
// The review screen: shows a card front, flips on Confirm, grades on the four
// buttons (Again/Hard/Good/Easy), applies session-based SM-2, rewrites the deck
// file, and advances. Opened from the deck picker (Home -> Flashcards) with a
// deck path. Scheduling is session-based (no clock): the global session counter
// (FlashcardSession) advances when the review cycle completes and the daily goal
// was met.
//
// Mirrors DictionaryDefinitionActivity's structure: loop() reads input and calls
// finish()/requestUpdate(); render(RenderLock&&) composes the frame and ends
// with GUI.drawButtonHints + displayBuffer().
#pragma once

#include <cstdint>
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
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { Empty, Front, Back, Done };

  FlashcardDeck deck_;
  std::string deckPath_;
  State state_ = State::Empty;
  uint32_t session_ = 0;          // global session, captured on entry
  bool backHoldHandled_ = false;  // guards the long-press-Back-to-exit gesture

  void gradeAndAdvance(Grade g);
  void afterGrade();
  std::string deckName() const;  // basename for the header

  // ---- Word wrap ----------------------------------------------------------
  // Greedy word-wrap `text` to `maxWidth` px at `fontId`, returning display
  // lines. Adapted from DictionaryDefinitionActivity's wrap.
  std::vector<std::string> wrapToWidth(int fontId, const std::string& text, int maxWidth) const;
  // Draw wrapped text as a centered block whose vertical center is `centerY`.
  void drawWrappedCentered(int fontId, const std::string& text, int centerY, int maxWidth, bool bold) const;
};
