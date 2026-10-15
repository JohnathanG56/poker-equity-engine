#include "catch_amalgamated.hpp"

#include <algorithm>
#include <string>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/range.hpp"

using namespace poker;

namespace {
std::size_t count(const char* text) { return parse_range(text).size(); }
bool has(const std::vector<CardSet>& r, const char* combo) {
  return std::find(r.begin(), r.end(), CardSet::parse(combo)) != r.end();
}
}  // namespace

TEST_CASE("range: single hand classes", "[range]") {
  CHECK(count("AA") == 6);
  CHECK(count("AKs") == 4);
  CHECK(count("AKo") == 12);
  CHECK(count("AK") == 16);
  CHECK(count("KA") == 16);  // order of ranks doesn't matter
  CHECK(count("76s") == 4);
  CHECK(count("aks") == 4);  // case-insensitive
  const auto aks = parse_range("AKs");
  CHECK(has(aks, "AhKh"));
  CHECK_FALSE(has(aks, "AhKd"));
  const auto ako = parse_range("AKo");
  CHECK(has(ako, "AhKd"));
  CHECK_FALSE(has(ako, "AhKh"));
}

TEST_CASE("range: plus notation", "[range]") {
  CHECK(count("QQ+") == 18);    // QQ KK AA
  CHECK(count("22+") == 78);    // all 13 pairs
  CHECK(count("ATo+") == 48);   // ATo AJo AQo AKo
  CHECK(count("ATs+") == 16);
  CHECK(count("K9s+") == 16);   // K9s KTs KJs KQs
  CHECK(count("T9s+") == 4);    // only T9s (kicker climbs to one below the T)
  CHECK(count("A2+") == 12 * 16);
  const auto r = parse_range("ATo+");
  CHECK(has(r, "AsKd"));
  CHECK(has(r, "AsTd"));
  CHECK_FALSE(has(r, "As9d"));
}

TEST_CASE("range: dash notation", "[range]") {
  CHECK(count("22-55") == 24);
  CHECK(count("55-22") == 24);
  CHECK(count("A2s-A5s") == 16);
  CHECK(count("A5s-A2s") == 16);
  CHECK(count("K9o-KJo") == 36);
  const auto r = parse_range("22-55");
  CHECK(has(r, "3c3d"));
  CHECK_FALSE(has(r, "6c6d"));
}

TEST_CASE("range: lists, explicit combos and dedupe", "[range]") {
  // The example from the README: 18 + 4 + 48 + 4 + 16
  CHECK(count("QQ+, AKs, ATo+, 76s, KQ") == 18 + 4 + 48 + 4 + 16);
  CHECK(count("QQ+,AKs ATo+") == 18 + 4 + 48);
  CHECK(count("AA, AA, AhAd") == 6);  // duplicates collapse
  CHECK(count("AhKh") == 1);
  CHECK(count("AhKh, AdKd") == 2);
  CHECK(count("random") == 1326);
  CHECK(count("any") == 1326);
  CHECK(all_combos().size() == 1326);
  // Every combo is two distinct cards
  for (CardSet c : parse_range("random")) CHECK(c.size() == 2);
}

TEST_CASE("range: invalid tokens throw", "[range]") {
  CHECK_THROWS_AS(parse_range(""), ParseError);
  CHECK_THROWS_AS(parse_range("AAs"), ParseError);
  CHECK_THROWS_AS(parse_range("AKx"), ParseError);
  CHECK_THROWS_AS(parse_range("A1s"), ParseError);
  CHECK_THROWS_AS(parse_range("AKs-QJs"), ParseError);   // different top cards
  CHECK_THROWS_AS(parse_range("A2s-A5o"), ParseError);   // mixed suitedness
  CHECK_THROWS_AS(parse_range("22-A5s"), ParseError);
  CHECK_THROWS_AS(parse_range("AhAh"), ParseError);
  CHECK_THROWS_AS(parse_range("AKQ"), ParseError);
}

TEST_CASE("range: removing conflicts with known cards", "[range]") {
  const auto aa = parse_range("AA");
  CHECK(remove_conflicts(aa, CardSet::parse("As")).size() == 3);
  CHECK(remove_conflicts(aa, CardSet::parse("As Ah")).size() == 1);
  CHECK(remove_conflicts(aa, CardSet::parse("Kd")).size() == 6);
  CHECK(remove_conflicts(all_combos(), CardSet::parse("As Ks")).size() == 1225);  // C(50,2)
}

TEST_CASE("PlayerSpec::parse distinguishes hands, ranges and random", "[range]") {
  CHECK(PlayerSpec::parse("AsKs").kind == PlayerSpec::Kind::Hand);
  CHECK(PlayerSpec::parse("As Ks").kind == PlayerSpec::Kind::Hand);
  CHECK(PlayerSpec::parse("AsKs").hand == CardSet::parse("As Ks"));
  CHECK(PlayerSpec::parse("AKs").kind == PlayerSpec::Kind::Range);
  CHECK(PlayerSpec::parse("QQ+, AKs").combos.size() == 22);
  CHECK(PlayerSpec::parse("random").kind == PlayerSpec::Kind::Random);
  CHECK(PlayerSpec::parse("").kind == PlayerSpec::Kind::Random);
  CHECK(PlayerSpec::parse("AhKh, AdKd").kind == PlayerSpec::Kind::Range);
  CHECK_THROWS_AS(PlayerSpec::parse("AsAs"), ParseError);
  CHECK_THROWS_AS(PlayerSpec::parse("ZZ"), ParseError);
}
