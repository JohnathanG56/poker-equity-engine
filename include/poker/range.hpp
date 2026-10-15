// range.hpp - Hand-range notation ("QQ+, AKs, ATo+, 76s, KQ, 22-55").
//
// A "combo" is one concrete two-card starting hand, e.g. {Ah, Kh}. A range is
// a set of combos. There are C(52,2) = 1326 combos in total:
//   pocket pair (e.g. "QQ")  -> 6 combos  (C(4,2) suit pairs)
//   suited      (e.g. "AKs") -> 4 combos  (one per suit)
//   offsuit     (e.g. "AKo") -> 12 combos (4 * 3 ordered suit pairs)
//   both        (e.g. "AK")  -> 16 combos
//
// Supported grammar (tokens separated by commas and/or whitespace):
//   QQ        a pocket pair
//   QQ+       QQ, KK, AA
//   22-55     22, 33, 44, 55 (either order)
//   AKs AKo AK
//   ATo+      ATo, AJo, AQo, AKo (kicker climbs up to one below the top card)
//   K9s+      K9s, KTs, KJs, KQs (same rule; note "T9s+" is just T9s)
//   A2s-A5s   A2s, A3s, A4s, A5s (same top card, kicker range)
//   AhKh      one explicit combo
//   random / any / 100%   all 1326 combos
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "poker/card.hpp"

namespace poker {

/// Parse range text into a sorted, de-duplicated list of 2-card combos.
/// Throws ParseError on malformed tokens.
std::vector<CardSet> parse_range(std::string_view text);

/// All 1326 two-card combos.
std::vector<CardSet> all_combos();

/// Remove every combo that shares a card with `blocked`.
std::vector<CardSet> remove_conflicts(const std::vector<CardSet>& combos, CardSet blocked);

}  // namespace poker
