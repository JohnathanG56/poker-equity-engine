// equity_common.hpp - internals shared by the exact and Monte Carlo engines.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/evaluator.hpp"

namespace poker::detail {

/// Pot shares are accumulated as integers in units of 1/2520 of a pot.
/// 2520 = lcm(1, 2, ..., 9), so a k-way split (k <= 9 players) is always an
/// exact integer (2520 / k). Integer accumulation means results are exact,
/// independent of summation order, and identical across thread counts merges.
inline constexpr std::uint64_t kShareUnits = 2520;

/// Per-player integer tallies.
struct Tally {
  std::uint64_t wins = 0;      // trials won outright
  std::uint64_t ties = 0;      // trials that ended in a split including this player
  std::uint64_t units = 0;     // sum of pot share, in 1/kShareUnits
  std::uint64_t units_sq = 0;  // sum of squared share (for the variance)
};

/// Score one showdown: strengths[i] for n players; adds to tallies.
inline void score_showdown(const std::uint32_t* strengths, int n, Tally* tallies) noexcept {
  std::uint32_t best = 0;
  for (int i = 0; i < n; ++i) best = strengths[i] > best ? strengths[i] : best;
  int winners = 0;
  for (int i = 0; i < n; ++i) winners += strengths[i] == best;
  const std::uint64_t share = kShareUnits / static_cast<std::uint64_t>(winners);
  for (int i = 0; i < n; ++i) {
    if (strengths[i] == best) {
      Tally& t = tallies[i];
      if (winners == 1) ++t.wins; else ++t.ties;
      t.units += share;
      t.units_sq += share * share;
    }
  }
}

inline void validate_player_count(std::size_t n) {
  if (n < static_cast<std::size_t>(kMinPlayers) || n > static_cast<std::size_t>(kMaxPlayers)) {
    throw std::invalid_argument("need between " + std::to_string(kMinPlayers) + " and " +
                                std::to_string(kMaxPlayers) + " players, got " +
                                std::to_string(n));
  }
}

inline void validate_board(CardSet board) {
  const int n = board.size();
  if (n != 0 && n != 3 && n != 4 && n != 5) {
    throw std::invalid_argument("board must have 0, 3, 4 or 5 cards, got " + std::to_string(n));
  }
}

/// Adds `cards` to `used`, throwing if any card is already present.
inline void claim_cards(CardSet& used, CardSet cards, const std::string& what) {
  if (used.intersects(cards)) {
    throw std::invalid_argument("card conflict: " + (used & cards).str() + " in " + what +
                                " is already in use");
  }
  used |= cards;
}

}  // namespace poker::detail
