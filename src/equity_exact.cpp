// equity_exact.cpp - exact equity by enumerating every board completion.
//
// With all hole cards known, the only randomness left is which k = 5 - |board|
// cards come off the deck. Every k-subset of the remaining deck is equally
// likely, so enumerating all C(remaining, k) of them and averaging the pot
// share gives the equity with zero sampling error. Heads-up preflop that is
// C(48, 5) = 1,712,304 boards; on the flop only C(45, 2) = 990.
#include <array>
#include <vector>

#include "equity_common.hpp"
#include "poker/equity.hpp"
#include "poker/evaluator.hpp"

namespace poker {

EquityResult equity_exact(const std::vector<CardSet>& hands, CardSet board, CardSet dead) {
  using namespace detail;
  validate_player_count(hands.size());
  validate_board(board);

  CardSet used;
  claim_cards(used, board, "board");
  claim_cards(used, dead, "dead cards");
  for (std::size_t i = 0; i < hands.size(); ++i) {
    if (hands[i].size() != 2) {
      throw std::invalid_argument("player " + std::to_string(i + 1) + " must have exactly 2 cards");
    }
    claim_cards(used, hands[i], "player " + std::to_string(i + 1) + "'s hand");
  }

  // The undealt deck as an array of single-bit masks.
  std::vector<std::uint64_t> deck;
  for (int c = 0; c < kNumCards; ++c) {
    if (!used.contains(Card(c))) deck.push_back(Card(c).mask());
  }
  const int k = 5 - board.size();
  const int d = static_cast<int>(deck.size());
  if (d < k) throw std::invalid_argument("not enough cards left in the deck to complete the board");

  const int n = static_cast<int>(hands.size());
  std::array<std::uint64_t, kMaxPlayers> hole{};
  for (int i = 0; i < n; ++i) hole[i] = hands[i].bits();
  std::array<Tally, kMaxPlayers> tally{};
  std::array<std::uint32_t, kMaxPlayers> strength{};
  std::uint64_t boards = 0;

  auto showdown = [&](std::uint64_t full_board) {
    for (int i = 0; i < n; ++i) strength[i] = evaluate7(hole[i] | full_board);
    score_showdown(strength.data(), n, tally.data());
    ++boards;
  };

  if (k == 0) {
    showdown(board.bits());
  } else {
    // Iterate over all k-combinations of deck indices in lexicographic order:
    // idx = {0, 1, ..., k-1}, then repeatedly bump the rightmost index that
    // still has room and reset everything to its right.
    std::array<int, 5> idx{};
    for (int j = 0; j < k; ++j) idx[j] = j;
    while (true) {
      std::uint64_t b = board.bits();
      for (int j = 0; j < k; ++j) b |= deck[idx[j]];
      showdown(b);

      int j = k - 1;
      while (j >= 0 && idx[j] == d - k + j) --j;
      if (j < 0) break;
      ++idx[j];
      for (int m = j + 1; m < k; ++m) idx[m] = idx[m - 1] + 1;
    }
  }

  EquityResult result;
  result.exact = true;
  result.trials = boards;
  const double total = static_cast<double>(boards);
  for (int i = 0; i < n; ++i) {
    PlayerEquity p;
    p.win = static_cast<double>(tally[i].wins) / total;
    p.tie = static_cast<double>(tally[i].ties) / total;
    p.equity = static_cast<double>(tally[i].units) / (total * static_cast<double>(kShareUnits));
    p.std_error = 0.0;
    result.players.push_back(p);
  }
  return result;
}

}  // namespace poker
