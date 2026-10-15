// reference_eval.hpp - A deliberately slow, obviously-correct evaluator used
// only to cross-check evaluate7. It shares no code or tricks with the fast
// evaluator: it sorts ranks, counts groups, and applies the rules of poker
// literally to every one of the 21 five-card subsets of a 7-card hand.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "poker/card.hpp"

namespace poker::reference {

/// Comparable value for a 5-card hand: category in the top hex digit, then up
/// to five rank "digits" in order of importance (zero-padded).
inline std::uint64_t eval5(const std::array<Card, 5>& hand) {
  std::array<int, 5> ranks{};
  bool flush = true;
  for (int i = 0; i < 5; ++i) {
    ranks[i] = hand[i].rank();
    if (hand[i].suit() != hand[0].suit()) flush = false;
  }
  std::sort(ranks.begin(), ranks.end(), std::greater<>());

  // Group by rank: (count, rank), sorted by count desc then rank desc.
  std::vector<std::pair<int, int>> groups;
  for (int r : ranks) {
    if (!groups.empty() && groups.back().second == r) {
      ++groups.back().first;
    } else {
      groups.emplace_back(1, r);
    }
  }
  std::sort(groups.begin(), groups.end(), std::greater<>());

  const bool five_distinct = groups.size() == 5;
  bool straight = false;
  int straight_high = -1;
  if (five_distinct && ranks[0] - ranks[4] == 4) {
    straight = true;
    straight_high = ranks[0];
  } else if (five_distinct && ranks[0] == 12 && ranks[1] == 3 && ranks[2] == 2 &&
             ranks[3] == 1 && ranks[4] == 0) {
    straight = true;  // the wheel: A-2-3-4-5 plays as 5-high
    straight_high = 3;
  }

  int category = 0;
  std::vector<int> order;  // ranks in order of importance
  if (straight && flush) {
    category = 8;
    order = {straight_high};
  } else if (groups[0].first == 4) {
    category = 7;
    order = {groups[0].second, groups[1].second};
  } else if (groups[0].first == 3 && groups[1].first == 2) {
    category = 6;
    order = {groups[0].second, groups[1].second};
  } else if (flush) {
    category = 5;
    order.assign(ranks.begin(), ranks.end());
  } else if (straight) {
    category = 4;
    order = {straight_high};
  } else if (groups[0].first == 3) {
    category = 3;
    order = {groups[0].second, groups[1].second, groups[2].second};
  } else if (groups[0].first == 2 && groups[1].first == 2) {
    category = 2;
    order = {groups[0].second, groups[1].second, groups[2].second};
  } else if (groups[0].first == 2) {
    category = 1;
    order = {groups[0].second, groups[1].second, groups[2].second, groups[3].second};
  } else {
    category = 0;
    order.assign(ranks.begin(), ranks.end());
  }

  std::uint64_t value = static_cast<std::uint64_t>(category);
  for (int i = 0; i < 5; ++i) {
    value = value * 16 + (i < static_cast<int>(order.size()) ? static_cast<std::uint64_t>(order[i]) + 1 : 0);
  }
  return value;
}

/// Category (0..8) of a value returned by eval5 / eval7.
inline int category(std::uint64_t value) { return static_cast<int>(value >> 20); }

/// Best of all 21 five-card subsets of a 7-card hand.
inline std::uint64_t eval7(const std::array<Card, 7>& cards) {
  std::uint64_t best = 0;
  // Choose the two cards to leave out.
  for (int skip1 = 0; skip1 < 7; ++skip1) {
    for (int skip2 = skip1 + 1; skip2 < 7; ++skip2) {
      std::array<Card, 5> hand{};
      int k = 0;
      for (int i = 0; i < 7; ++i) {
        if (i != skip1 && i != skip2) hand[k++] = cards[i];
      }
      best = std::max(best, eval5(hand));
    }
  }
  return best;
}

}  // namespace poker::reference
