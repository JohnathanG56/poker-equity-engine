// equity.hpp - All-in equity: exact enumeration and Monte Carlo simulation.
//
// "Equity" = the share of the pot a player wins on average if all remaining
// board cards are dealt with no more betting: P(win) + sum over split pots of
// P(split k ways) / k.
//
// Two engines:
//   * equity_exact        - every hole card known; enumerate every possible
//                           completion of the board. Zero sampling error.
//   * equity_monte_carlo  - players may be known hands, ranges or "random";
//                           sample deals at random. Reports a standard error.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "poker/card.hpp"

namespace poker {

inline constexpr int kMinPlayers = 2;
inline constexpr int kMaxPlayers = 9;

/// What we know about one player's hole cards.
struct PlayerSpec {
  enum class Kind { Hand, Range, Random };

  Kind kind = Kind::Random;
  CardSet hand;                  // Kind::Hand: exactly two cards
  std::vector<CardSet> combos;   // Kind::Range: candidate combos (unfiltered)
  std::string label;             // what the user typed (for display)

  static PlayerSpec from_hand(CardSet hand);
  static PlayerSpec from_range(std::string_view range_text);
  static PlayerSpec random();
  /// "AsKs" / "As Ks" -> Hand; "random" / "any" / "?" -> Random; else Range.
  static PlayerSpec parse(std::string_view text);
};

struct PlayerEquity {
  double win = 0.0;        // fraction of trials won outright
  double tie = 0.0;        // fraction of trials ending in a split pot
  double equity = 0.0;     // win + fractional share of split pots
  double std_error = 0.0;  // standard error of `equity` (0 for exact)
};

struct EquityResult {
  std::vector<PlayerEquity> players;
  std::uint64_t trials = 0;    // boards enumerated (exact) or samples (MC)
  std::uint64_t rejected = 0;  // MC only: deals discarded for card conflicts
  bool exact = false;
};

/// Exact equity by enumerating every board completion.
/// `hands`: 2..9 known two-card hands. `board`: 0, 3, 4 or 5 cards.
/// `dead`: cards known to be out of the deck (e.g. mucked). Throws
/// std::invalid_argument on invalid input (overlapping cards, wrong sizes).
EquityResult equity_exact(const std::vector<CardSet>& hands, CardSet board = {},
                          CardSet dead = {});

/// Monte Carlo equity. Each sample deals every non-fixed player a combo
/// (uniformly from their range, rejecting the whole deal on a card conflict),
/// then completes the board uniformly from the remaining deck.
///
/// Deterministic for a given (seed, threads). `threads == 0` means
/// std::thread::hardware_concurrency().
EquityResult equity_monte_carlo(const std::vector<PlayerSpec>& players, CardSet board,
                                CardSet dead, std::uint64_t samples, std::uint64_t seed,
                                unsigned threads = 1);

}  // namespace poker
