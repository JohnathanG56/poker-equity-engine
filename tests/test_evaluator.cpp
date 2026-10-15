#include "catch_amalgamated.hpp"

#include <array>
#include <string>

#include "poker/card.hpp"
#include "poker/evaluator.hpp"

using namespace poker;

namespace {
std::uint32_t ev(const char* cards) { return evaluate7(CardSet::parse(cards).bits()); }
HandCategory cat(const char* cards) { return category_of(ev(cards)); }
}  // namespace

TEST_CASE("bit helpers", "[evaluator][bits]") {
  using namespace detail;
  // compress_nibbles: bit 4r -> bit r
  STATIC_REQUIRE(compress_nibbles(0) == 0);
  STATIC_REQUIRE(compress_nibbles(1) == 1);
  STATIC_REQUIRE(compress_nibbles(1ULL << 48) == (1u << 12));
  STATIC_REQUIRE(compress_nibbles(kSuitBits) == 0x1FFF);
  STATIC_REQUIRE(compress_nibbles((1ULL << 4) | (1ULL << 20)) == ((1u << 1) | (1u << 5)));
  for (int r = 0; r < 13; ++r) {
    REQUIRE(compress_nibbles(1ULL << (4 * r)) == (1u << r));
  }
  STATIC_REQUIRE(keep_top_n(0b1011011, 3) == 0b1011000);
  STATIC_REQUIRE(keep_top1(0b0110) == 0b0100);
  STATIC_REQUIRE(keep_top1(0) == 0);
  // straights: 0b11111 = 2..6 -> high is rank 4 ('6')
  STATIC_REQUIRE(straight_high_plus1(0b11111) == 4 + 1);
  // wheel A,2,3,4,5 -> high is rank 3 ('5')
  STATIC_REQUIRE(straight_high_plus1((1u << 12) | 0b1111) == 3 + 1);
  // broadway T..A -> high rank 12
  STATIC_REQUIRE(straight_high_plus1(0x1F00) == 12 + 1);
  // no straight with a gap
  STATIC_REQUIRE(straight_high_plus1(0b1101111 & ~0b100) == 0);
  STATIC_REQUIRE(kTop5[0x1FFF] == 0x1F00);
}

TEST_CASE("every category is detected", "[evaluator]") {
  CHECK(cat("As Ks Qs Js Ts 2d 3c") == HandCategory::StraightFlush);
  CHECK(cat("9h 9d 9s 9c Kd 2c 3h") == HandCategory::FourOfAKind);
  CHECK(cat("9h 9d 9s Kc Kd 2c 3h") == HandCategory::FullHouse);
  CHECK(cat("Ah 9h 7h 4h 2h Kd Qc") == HandCategory::Flush);
  CHECK(cat("9c Td Jh Qs Kd 2c 3h") == HandCategory::Straight);
  CHECK(cat("7c 7d 7h As Kd 2c 3h") == HandCategory::ThreeOfAKind);
  CHECK(cat("7c 7d Kh Ks Ad 2c 3h") == HandCategory::TwoPair);
  CHECK(cat("7c 7d Kh Qs Ad 2c 4h") == HandCategory::Pair);
  CHECK(cat("7c 9d Kh Qs Ad 2c 4h") == HandCategory::HighCard);
}

TEST_CASE("categories are ordered", "[evaluator]") {
  const std::array<const char*, 9> ladder = {
      "7c 9d Kh Qs Ad 2c 4h",  // high card
      "7c 7d Kh Qs Ad 2c 4h",  // pair
      "7c 7d Kh Ks Ad 2c 3h",  // two pair
      "7c 7d 7h As Kd 2c 3h",  // trips
      "9c Td Jh Qs Kd 2c 3h",  // straight
      "Ah 9h 7h 4h 2h Kd Qc",  // flush
      "9h 9d 9s Kc Kd 2c 3h",  // full house
      "9h 9d 9s 9c Kd 2c 3h",  // quads
      "As Ks Qs Js Ts 2d 3c",  // straight flush
  };
  for (std::size_t i = 1; i < ladder.size(); ++i) {
    INFO(ladder[i - 1] << " < " << ladder[i]);
    CHECK(ev(ladder[i - 1]) < ev(ladder[i]));
  }
}

TEST_CASE("kickers are compared in order", "[evaluator]") {
  // Same pair, better first kicker
  CHECK(ev("Kc Kd Ah 9s 7d 4c 2h") > ev("Kc Kd Qh Js 7d 4c 2h"));
  // Same pair, first kicker equal, second kicker decides
  CHECK(ev("Kc Kd Ah Ts 7d 4c 2h") > ev("Kh Ks Ac 9s 7c 4d 2d"));
  // Only the best 3 kickers play with a pair: the 5th-best card doesn't matter
  CHECK(ev("Kc Kd Ah Ts 7d 4c 2h") == ev("Kh Ks Ac Td 7c 3d 2d"));
  // High card: 5th card decides, 6th and 7th don't
  CHECK(ev("Ac Qd 9h 7s 5d 3c 2h") > ev("Ad Qh 9c 7d 4s 3h 2c"));
  CHECK(ev("Ac Qd 9h 7s 5d 3c 2h") == ev("Ad Qh 9c 7d 5s 4h 2c"));
  // Quads kicker
  CHECK(ev("9h 9d 9s 9c Ad 2c 3h") > ev("9h 9d 9s 9c Kd Qc Jh"));
  // Trips kickers
  CHECK(ev("7c 7d 7h Ac Qd 3c 2h") > ev("7c 7d 7h Ac Jd 3c 2h"));
  // Two pair: higher top pair beats higher bottom pair
  CHECK(ev("Ac Ad 3h 3s 5d 6c 9h") > ev("Kc Kd Qh Qs 5d 6c 9h"));
  // Two pair: same pairs, kicker decides
  CHECK(ev("Ac Ad 3h 3s Kd 6c 9h") > ev("Ac Ad 3h 3s Qd 6c 9h"));
  // Full house: trips rank first
  CHECK(ev("3c 3d 3h 2s 2d Kc Qh") > ev("2c 2d 2h As Ad Kc Qh"));
  // Flush: compared card by card
  CHECK(ev("Ah Kh 9h 7h 2h 3c 4d") > ev("Ah Qh Jh Th 8h 3c 4d"));
  CHECK(ev("Ah Kh 9h 7h 3h 2c 4d") > ev("Ah Kh 9h 7h 2h 3c 4d"));
}

TEST_CASE("exact ties compare equal", "[evaluator]") {
  // Board plays for both: royal on the board
  CHECK(ev("As Ks Qs Js Ts 2d 3c") == ev("As Ks Qs Js Ts 4h 5h"));
  // Same straight, different suits
  CHECK(ev("9c Td Jh Qs Kd 2c 3h") == ev("9d Th Js Qc Kh 2d 3s"));
  // Same two pair + kicker, different suits / irrelevant low cards
  CHECK(ev("Ac Ad 3h 3s Kd 6c 2h") == ev("Ah As 3c 3d Kc 5c 4h"));
  // Same flush ranks in different suits
  CHECK(ev("Ah 9h 7h 4h 2h Kd Qc") == ev("As 9s 7s 4s 2s Kc Qd"));
}

TEST_CASE("wheel and steel wheel", "[evaluator]") {
  const auto wheel = ev("Ac 2d 3h 4s 5d Kc 9h");
  CHECK(category_of(wheel) == HandCategory::Straight);
  // The wheel is the lowest straight: 6-high beats it
  CHECK(ev("6c 2d 3h 4s 5d Kc 9h") > wheel);
  // A-2-3-4-5-6: the six-high straight is chosen, not the wheel
  CHECK(ev("Ac 2d 3h 4s 5d 6c 9h") == ev("6c 2d 3h 4s 5d Kc 9h"));
  CHECK(describe(wheel) == "Straight, Five high");

  const auto steel = ev("As 2s 3s 4s 5s Kd 9h");
  CHECK(category_of(steel) == HandCategory::StraightFlush);
  CHECK(describe(steel) == "Straight Flush, Five high");
  CHECK(ev("6s 2s 3s 4s 5s Kd 9h") > steel);
  // Steel wheel beats any quads
  CHECK(steel > ev("Ac Ad Ah As Kd 9h 2c"));
  // Q-K-A-2-3 is NOT a straight (no wrap-around)
  CHECK(cat("Qc Kd Ah 2s 3d 7c 8h") == HandCategory::HighCard);
}

TEST_CASE("flush beats straight; straight flush needs one suit", "[evaluator]") {
  // 7 cards containing both a flush and a straight (different cards)
  const auto both = ev("5h 6h 7c 8h 9d Kh 2h");
  CHECK(category_of(both) == HandCategory::Flush);
  // Straight in mixed suits + flush elsewhere is a flush, not a straight flush
  CHECK(cat("4h 5h 6h 7h 8c 2h 9d") == HandCategory::Flush);
  // Straight flush hidden in a 6-card flush: 4..8 of hearts + 2h
  CHECK(cat("4h 5h 6h 7h 8h 2h 9d") == HandCategory::StraightFlush);
  CHECK(describe(ev("4h 5h 6h 7h 8h 2h 9d")) == "Straight Flush, Eight high");
  // Seven-card flush keeps the best five
  CHECK(ev("Ah Kh 9h 7h 5h 3h 2h") == ev("Ah Kh 9h 7h 5h 4c 2d"));
}

TEST_CASE("two pair when three pairs are present", "[evaluator]") {
  const auto three_pairs = ev("Kc Kd 7h 7s 4d 4c Ah");
  CHECK(category_of(three_pairs) == HandCategory::TwoPair);
  CHECK(three_pairs == ev("Kh Ks 7c 7d 2d 3c Ac"));
  // Third pair can act as the kicker: KK 77 44 + 2 -> K K 7 7 4
  const auto kicker_from_pair = ev("Kc Kd 7h 7s 4d 4c 2h");
  CHECK(kicker_from_pair == ev("Kh Ks 7c 7d 4h 3c 2c"));
  CHECK(describe(kicker_from_pair) == "Two Pair, Kings and Sevens (Four kicker)");
}

TEST_CASE("full house from two sets of trips", "[evaluator]") {
  const auto two_trips = ev("Kc Kd Kh 7s 7d 7c 2h");
  CHECK(category_of(two_trips) == HandCategory::FullHouse);
  CHECK(two_trips == ev("Kc Kd Kh 7s 7d Ac 2h"));
  CHECK(describe(two_trips) == "Full House, Kings full of Sevens");
  // Trips + two pairs: best pair is used
  CHECK(ev("7c 7d 7h Ks Kd 2c 2h") == ev("7c 7d 7h Ks Kd 3c 4h"));
  // Quads + trips is quads with best remaining card as kicker
  CHECK(cat("7c 7d 7h 7s Kd Kc Kh") == HandCategory::FourOfAKind);
  CHECK(ev("7c 7d 7h 7s Kd Kc Kh") == ev("7c 7d 7h 7s Kd 2c 3h"));
}

TEST_CASE("works for 5 and 6 card hands", "[evaluator]") {
  CHECK(cat("As Ks Qs Js Ts") == HandCategory::StraightFlush);
  CHECK(cat("2c 2d 5h 9s Jd") == HandCategory::Pair);
  CHECK(cat("2c 2d 5h 5s Jd Jc") == HandCategory::TwoPair);
}

TEST_CASE("describe produces readable names", "[evaluator]") {
  CHECK(describe(ev("As Ks Qs Js Ts 2d 3c")) == "Royal Flush");
  CHECK(describe(ev("9h 9d 9s 9c Kd 2c 3h")) == "Four of a Kind, Nines");
  CHECK(describe(ev("7c 7d Kh Qs Ad 2c 4h")) == "Pair of Sevens");
  CHECK(describe(ev("7c 9d Kh Qs Ad 2c 4h")) == "High Card, Ace");
  CHECK(std::string(category_name(HandCategory::Flush)) == "Flush");
}
