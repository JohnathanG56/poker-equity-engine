#include <catch2/catch_test_macros.hpp>

#include "poker/card.hpp"

using namespace poker;

TEST_CASE("card encoding is rank * 4 + suit", "[card]") {
  STATIC_REQUIRE(Card(0, 0).index() == 0);    // 2c
  STATIC_REQUIRE(Card(12, 3).index() == 51);  // As
  STATIC_REQUIRE(Card(51).rank() == 12);
  STATIC_REQUIRE(Card(51).suit() == 3);
  STATIC_REQUIRE(Card(5).mask() == (std::uint64_t{1} << 5));
}

TEST_CASE("rank and suit characters", "[card]") {
  REQUIRE(rank_from_char('A') == 12);
  REQUIRE(rank_from_char('t') == 8);
  REQUIRE(rank_from_char('1') == -1);
  REQUIRE(suit_from_char('s') == 3);
  REQUIRE(suit_from_char('x') == -1);
}
