// src/flashcards/FlashcardDeck.h
//
// Loads a .tsv deck + its .srs scheduling sidecar off the SD card, builds the
// "due today" queue, applies SM-2 on each grade, and persists state atomically.
// No BLE, no phone — decks are files the user drops in /flashcards, mirroring
// how the reader loads books from /books.
//
// Design notes:
//  * Card identity is a hash of front+back (not line number), so reordering or
//    inserting lines in the .tsv never scrambles a user's scheduling state.
//  * Only the *due* subset is held in RAM (cap MAX_DUE) — the C3 heap is tight
//    (see Dictionary's LowMemory failure mode); do not load whole decks.
//  * "Today" is passed IN as an epoch-day (days since 1970) so this class stays
//    pure and host-testable. The Activity supplies it from the RTC.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Sm2.h"

class FlashcardDeck {
 public:
  static constexpr size_t MAX_DUE = 200;  // resident due cards; overflow reviewed next session

  struct Card {
    uint32_t hash = 0;
    std::string front;
    std::string back;
    SrsState srs;      // live scheduling state (from sidecar, or defaults for a new card)
    uint32_t due = 0;  // epoch-day the card is next due (0 = brand new, due immediately)
  };

  // Load `<tsvPath>` and merge `<tsvPath-with-.srs>`; build the queue of cards
  // due on/before `todayEpochDay`. Returns false if the .tsv can't be opened.
  bool load(const std::string& tsvPath, uint32_t todayEpochDay);

  size_t totalCards() const { return totalCount_; }  // cards in the .tsv
  size_t dueCount() const { return due_.size() - cursor_; }  // remaining this session
  size_t reviewedThisSession() const { return reviewed_; }
  bool overflowed() const { return overflowed_; }  // more were due than MAX_DUE

  // The card currently under review, or nullptr when the session is done.
  const Card* current() const;

  // Apply a grade (Again=1 / Good=4) to the current card, persist the sidecar,
  // and advance. No-op if the session is already done.
  void grade(int quality, uint32_t todayEpochDay);

  // Append a new card to the .tsv (used by the on-device keyboard add flow and
  // the dictionary "save word" hook). Also creates the deck/dir if absent.
  // Does NOT insert it into the current due queue — it lands next session.
  static bool appendCard(const std::string& tsvPath, const std::string& front, const std::string& back);

  // Count the cards in a deck file (header/comment/blank lines skipped) by
  // streaming it once, without building the due queue or touching the sidecar.
  // Cheap enough for the deck picker to call per deck on entry.
  static size_t countCards(const std::string& tsvPath);

 private:
  // Tiny per-card scheduling record — ALL cards' state lives here (a few bytes
  // each), even ones not due today, so the sidecar can be rewritten completely
  // on every grade. Card TEXT (front/back strings) is what MAX_DUE caps, not
  // these records.
  struct SrsRec {
    uint32_t hash = 0;
    uint32_t due = 0;
    SrsState srs;
  };

  std::string tsvPath_;
  std::string srsPath_;
  std::vector<Card> due_;    // due-today cards WITH text (capped at MAX_DUE)
  std::vector<SrsRec> srs_;  // every known card's scheduling state (cheap)
  size_t cursor_ = 0;
  size_t totalCount_ = 0;
  size_t reviewed_ = 0;
  bool overflowed_ = false;

  SrsRec* findRec(uint32_t hash);  // nullptr if the card is new (never graded)

  // Persist every known card's SRS state to `<srsPath_>` using the crash-safe
  // temp-then-rename pattern (see .cpp). Called after each grade.
  bool saveSidecar();
};
