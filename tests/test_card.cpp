#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

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
  REQUIRE(rank_from_char('2') == 0);
  REQUIRE(rank_from_char('1') == -1);
  REQUIRE(suit_from_char('s') == 3);
  REQUIRE(suit_from_char('C') == 0);
  REQUIRE(suit_from_char('x') == -1);
}

TEST_CASE("all 52 cards round-trip through text", "[card]") {
  std::set<std::string> seen;
  for (int i = 0; i < kNumCards; ++i) {
    const Card c(i);
    const std::string s = c.str();
    REQUIRE(s.size() == 2);
    REQUIRE(Card::parse(s) == c);
    REQUIRE(Card::parse(s).index() == i);
    seen.insert(s);
  }
  REQUIRE(seen.size() == 52);
}

TEST_CASE("specific card parses", "[card]") {
  REQUIRE(Card::parse("As") == Card(12, 3));
  REQUIRE(Card::parse("Td") == Card(8, 1));
  REQUIRE(Card::parse("7c") == Card(5, 0));
  REQUIRE(Card::parse("2h") == Card(0, 2));
  REQUIRE(Card::parse("kS") == Card(11, 3));  // case-insensitive
}

TEST_CASE("bad card text throws ParseError", "[card]") {
  REQUIRE_THROWS_AS(Card::parse(""), ParseError);
  REQUIRE_THROWS_AS(Card::parse("A"), ParseError);
  REQUIRE_THROWS_AS(Card::parse("Asx"), ParseError);
  REQUIRE_THROWS_AS(Card::parse("1s"), ParseError);
  REQUIRE_THROWS_AS(Card::parse("Ax"), ParseError);
  REQUIRE_THROWS_AS(Card::parse("10h"), ParseError);
  REQUIRE_THROWS_AS(Card::parse("sA"), ParseError);
}

TEST_CASE("CardSet parses several separator styles", "[cardset]") {
  const CardSet expected{Card::parse("As"), Card::parse("Ks")};
  REQUIRE(CardSet::parse("AsKs") == expected);
  REQUIRE(CardSet::parse("As Ks") == expected);
  REQUIRE(CardSet::parse("As,Ks") == expected);
  REQUIRE(CardSet::parse("  As ,  Ks ") == expected);
  REQUIRE(CardSet::parse("").empty());
  REQUIRE(CardSet::parse("Jc Tc 2d").size() == 3);
}

TEST_CASE("CardSet rejects duplicates and garbage", "[cardset]") {
  REQUIRE_THROWS_AS(CardSet::parse("AsAs"), ParseError);
  REQUIRE_THROWS_AS(CardSet::parse("As Kd as"), ParseError);  // case-insensitive dup
  REQUIRE_THROWS_AS(CardSet::parse("AsK"), ParseError);
  REQUIRE_THROWS_AS(CardSet::parse("As K d"), ParseError);
  REQUIRE_THROWS_AS(CardSet::parse("AsXx"), ParseError);
}

TEST_CASE("CardSet set operations and printing", "[cardset]") {
  CardSet s = CardSet::parse("7c As Kd");
  REQUIRE(s.size() == 3);
  REQUIRE(s.contains(Card::parse("Kd")));
  REQUIRE_FALSE(s.contains(Card::parse("Kh")));
  REQUIRE(s.str() == "As Kd 7c");  // printed high to low

  s.remove(Card::parse("As"));
  REQUIRE(s.size() == 2);
  REQUIRE(s.intersects(CardSet::parse("Kd")));
  REQUIRE_FALSE(s.intersects(CardSet::parse("Ah Ac")));

  const auto cards = s.cards();
  REQUIRE(cards.size() == 2);
  REQUIRE(cards[0].index() < cards[1].index());  // ascending

  REQUIRE(CardSet{kFullDeck}.size() == 52);
  REQUIRE(CardSet::parse(CardSet{kFullDeck}.str()) == CardSet{kFullDeck});
}
