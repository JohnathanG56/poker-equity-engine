// evaluator.hpp - 5/6/7-card Texas Hold'em hand evaluator.
//
//   std::uint32_t evaluate7(std::uint64_t cards);
//
// Input is a CardSet bitmask (see card.hpp: bit = rank * 4 + suit). The output
// is a "strength" integer: higher is better, equal values are exact ties. That
// makes showdown logic trivial: compute one integer per player, take the max.
//
// Strength layout (30 bits used):
//
//    bits 26..29  category   (0 = high card ... 8 = straight flush)
//    bits 13..25  major mask (13-bit rank mask: the ranks that form the hand)
//    bits  0..12  minor mask (13-bit rank mask: kickers / the pair in a boat)
//
// Within one category, the major masks being compared always have the same
// number of bits set (e.g. two pair -> exactly 2 bits), and the same holds for
// the minor masks. For two masks with equal popcount, plain integer comparison
// is the same as comparing their ranks highest-first ("lexicographic"), because
// the highest differing bit decides. So one integer compare implements all of
// poker's kicker rules. Example:
//
//   Two pair, K K 7 7 with A kicker:
//     category = 2, major = bit(K) | bit(7), minor = bit(A)
//   Two pair, K K 7 7 with Q kicker:  same major, smaller minor -> loses
//   Two pair, K K 8 8 with 2 kicker:  bigger major (bit 8 > bit 7) -> wins
//
// The evaluator is branch-light, allocation-free and uses two 8192-entry
// (16 KB each) lookup tables built at compile time, so it also fits comfortably
// in a WebAssembly build.
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <string>

#include "poker/card.hpp"

namespace poker {

enum class HandCategory : std::uint8_t {
  HighCard = 0,
  Pair = 1,
  TwoPair = 2,
  ThreeOfAKind = 3,
  Straight = 4,
  Flush = 5,
  FullHouse = 6,
  FourOfAKind = 7,
  StraightFlush = 8,
};

inline constexpr int kCategoryShift = 26;
inline constexpr int kMajorShift = 13;

/// Extract the category from a strength value.
constexpr HandCategory category_of(std::uint32_t strength) noexcept {
  return static_cast<HandCategory>(strength >> kCategoryShift);
}

/// "Two Pair", "Flush", ...
const char* category_name(HandCategory c) noexcept;

/// Human-readable description, e.g. "Two Pair, Kings and Sevens (Ace kicker)".
std::string describe(std::uint32_t strength);

namespace detail {

// ---------------------------------------------------------------------------
// Bit-trick helpers. Every function here is constexpr, so the lookup tables
// below are computed entirely by the compiler: zero startup cost.
// ---------------------------------------------------------------------------

// One bit at the bottom of each of the 13 rank nibbles: 0x0001'1111'1111'1111.
// (cards >> s) & kSuitBits isolates suit `s`: bit 4r is set iff (rank r, suit s)
// is in the hand.
inline constexpr std::uint64_t kSuitBits = 0x0001'1111'1111'1111ULL;

/// "Compress" a mask whose interesting bits sit at positions 0, 4, 8, ..., 60
/// (one per nibble) into 16 contiguous bits. This is a portable version of the
/// x86 BMI2 `pext` instruction for this fixed pattern, done as a log-step
/// "gather": each step halves the gaps between the bits we keep.
///
///   start:   bits at 0,4,8,12, 16,20,24,28, ...      (gap 4)
///   step 1:  x |= x >> 3   moves bit 4  -> 1 (pairs within each byte)
///            mask 0x03 per byte keeps bits {0,1} of every byte
///   step 2:  x |= x >> 6   moves byte-pair's bits 8,9 -> 2,3
///            mask 0x000F per 16-bit lane keeps 4 bits per lane
///   step 3:  x |= x >> 12  moves lane bits 16..19 -> 4..7
///            mask 0x00FF per 32-bit half keeps 8 bits per half
///   step 4:  x |= x >> 24  moves bits 32..39 -> 8..15, keep low 16 bits.
///
/// Result: bit r of the output = bit 4r of the input.
constexpr std::uint32_t compress_nibbles(std::uint64_t x) noexcept {
  x = (x | (x >> 3)) & 0x0303'0303'0303'0303ULL;
  x = (x | (x >> 6)) & 0x000F'000F'000F'000FULL;
  x = (x | (x >> 12)) & 0x0000'00FF'0000'00FFULL;
  x = (x | (x >> 24)) & 0x0000'0000'0000'FFFFULL;
  return static_cast<std::uint32_t>(x);
}

/// Index of the highest set bit (m must be non-zero).
constexpr int top_bit(std::uint32_t m) noexcept { return std::bit_width(m) - 1; }

/// Keep only the highest set bit.
constexpr std::uint32_t keep_top1(std::uint32_t m) noexcept {
  return m ? (1u << top_bit(m)) : 0u;
}

/// Keep only the `n` highest set bits by repeatedly clearing the LOWEST set
/// bit: `m & (m - 1)` turns off the lowest 1 (subtracting 1 flips it and every
/// 0 below it; AND-ing with the original clears exactly that bit).
constexpr std::uint32_t keep_top_n(std::uint32_t m, int n) noexcept {
  while (std::popcount(m) > n) m &= m - 1;
  return m;
}

/// If the 13-bit rank mask contains a straight, return (high rank + 1), else 0.
///
/// Trick: AND the mask with itself shifted right by 1, 2, 3 and 4. Bit j
/// survives only if bits j, j+1, j+2, j+3, j+4 are all set, i.e. a 5-long run
/// starting at bit j. The wheel (A-2-3-4-5) needs the Ace to also act as rank "-1": we
/// shift the whole mask up by one and put a copy of the Ace bit at bit 0, so
/// in the shifted world bit (r+1) means rank r and bit 0 means "low Ace".
constexpr int straight_high_plus1(std::uint32_t ranks) noexcept {
  const std::uint32_t ace = (ranks >> 12) & 1u;
  const std::uint32_t m = (ranks << 1) | ace;  // 14 bits: low-Ace, 2, ..., A
  const std::uint32_t run = m & (m >> 1) & (m >> 2) & (m >> 3) & (m >> 4);
  // `run` bit j set means bits j..j+4 of m are set, i.e. a straight whose
  // HIGHEST card sits at bit j+4 of m, which is rank j+3. We want the best
  // straight, so take the highest such j.
  if (run == 0) return 0;
  return top_bit(run) + 3 + 1;  // +1 so that 0 can mean "no straight"
}

using RankTable = std::array<std::uint16_t, 8192>;
using StraightTable = std::array<std::uint8_t, 8192>;

constexpr RankTable make_top5_table() {
  RankTable t{};
  for (std::uint32_t m = 0; m < 8192; ++m) t[m] = static_cast<std::uint16_t>(keep_top_n(m, 5));
  return t;
}

constexpr StraightTable make_straight_table() {
  StraightTable t{};
  for (std::uint32_t m = 0; m < 8192; ++m) t[m] = static_cast<std::uint8_t>(straight_high_plus1(m));
  return t;
}

/// kTop5[m]  = m with only its 5 highest bits kept (flush / high-card hands).
inline constexpr RankTable kTop5 = make_top5_table();
/// kStraight[m] = (high rank of best straight in m) + 1, or 0 if none.
inline constexpr StraightTable kStraight = make_straight_table();

constexpr std::uint32_t make_strength(HandCategory c, std::uint32_t major,
                                      std::uint32_t minor) noexcept {
  return (static_cast<std::uint32_t>(c) << kCategoryShift) | (major << kMajorShift) | minor;
}

}  // namespace detail

/// Evaluate the best 5-card poker hand contained in `cards` (5, 6 or 7 cards;
/// fewer also works and simply produces a weaker category). Higher is better;
/// equal results tie.
constexpr std::uint32_t evaluate7(std::uint64_t cards) noexcept {
  using namespace detail;

  // -- 1. Split the hand into four 13-bit "which ranks do I hold" masks, one
  //       per suit. Shifting by s moves suit s's bits to the bottom of each
  //       nibble; kSuitBits keeps exactly those; compress packs them together.
  const std::uint32_t clubs = compress_nibbles(cards & kSuitBits);
  const std::uint32_t diamonds = compress_nibbles((cards >> 1) & kSuitBits);
  const std::uint32_t hearts = compress_nibbles((cards >> 2) & kSuitBits);
  const std::uint32_t spades = compress_nibbles((cards >> 3) & kSuitBits);

  // -- 2. Flush: some suit holds >= 5 cards. With at most 7 cards only one suit
  //       can qualify, so the first hit is the flush suit.
  std::uint32_t flush = 0;
  if (std::popcount(clubs) >= 5) flush = clubs;
  else if (std::popcount(diamonds) >= 5) flush = diamonds;
  else if (std::popcount(hearts) >= 5) flush = hearts;
  else if (std::popcount(spades) >= 5) flush = spades;

  if (flush) {
    // Straight flush: a straight using only the flush suit's ranks.
    if (const int sf = kStraight[flush]) {
      return make_strength(HandCategory::StraightFlush, 1u << (sf - 1), 0);
    }
    // (A plain flush is returned further down: quads / full house would beat
    //  it, although with 7 cards neither can coexist with a flush.)
  }

  // -- 3. Count how many suits hold each rank, "bit-sliced": treat the four
  //       suit masks as four 1-bit numbers per rank and add them with a tiny
  //       ripple-carry adder that processes all 13 ranks in parallel.
  //
  //   count(rank) = c + d + h + s  in {0..4}, stored as bits (b2 b1 b0).
  const std::uint32_t x1 = clubs ^ diamonds;   // half adder #1: sum bit
  const std::uint32_t c1 = clubs & diamonds;   //                carry
  const std::uint32_t x2 = hearts ^ spades;    // half adder #2: sum bit
  const std::uint32_t c2 = hearts & spades;    //                carry
  const std::uint32_t b0 = x1 ^ x2;            // ones bit of the total
  const std::uint32_t c3 = x1 & x2;            // carry out of the ones column
  // twos column = c1 + c2 + c3. c3 can only be 1 when c1 = c2 = 0 (x1 = 1
  // means exactly one of clubs/diamonds), so the twos column is at most 2:
  const std::uint32_t b1 = c1 ^ c2 ^ c3;       // twos bit
  const std::uint32_t b2 = c1 & c2;            // fours bit (all four suits)

  const std::uint32_t any = clubs | diamonds | hearts | spades;  // ranks held
  const std::uint32_t quads = b2;                // count == 4  (100)
  const std::uint32_t trips = b1 & b0;           // count == 3  (011)
  const std::uint32_t pairs = b1 & ~b0;          // count == 2  (010)

  // -- 4. Walk the categories from strongest to weakest.
  if (quads) {
    const std::uint32_t q = keep_top1(quads);
    return make_strength(HandCategory::FourOfAKind, q, keep_top1(any & ~q));
  }

  if (trips && (std::popcount(trips) >= 2 || pairs)) {
    // Full house. With two sets of trips (e.g. KKK 777 x) the lower trips act
    // as the pair, so the pair candidate is "other trips OR pairs".
    const std::uint32_t t = keep_top1(trips);
    return make_strength(HandCategory::FullHouse, t, keep_top1((trips & ~t) | pairs));
  }

  if (flush) return make_strength(HandCategory::Flush, kTop5[flush], 0);

  if (const int st = kStraight[any]) {
    return make_strength(HandCategory::Straight, 1u << (st - 1), 0);
  }

  if (trips) {
    return make_strength(HandCategory::ThreeOfAKind, trips, keep_top_n(any & ~trips, 2));
  }

  if (std::popcount(pairs) >= 2) {
    // Three pairs is possible with 7 cards: only the top two count, and the
    // third pair's rank becomes a kicker candidate.
    const std::uint32_t two = keep_top_n(pairs, 2);
    return make_strength(HandCategory::TwoPair, two, keep_top1(any & ~two));
  }

  if (pairs) {
    return make_strength(HandCategory::Pair, pairs, keep_top_n(any & ~pairs, 3));
  }

  return make_strength(HandCategory::HighCard, kTop5[any], 0);
}

/// Convenience overload.
constexpr std::uint32_t evaluate(CardSet cards) noexcept { return evaluate7(cards.bits()); }

}  // namespace poker
