#include <algorithm>
#include <cctype>
#include <string>

#include "poker/equity.hpp"
#include "poker/range.hpp"

namespace poker {

namespace {
std::string trim(std::string_view s) {
  std::size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}
}  // namespace

PlayerSpec PlayerSpec::from_hand(CardSet hand) {
  if (hand.size() != 2) throw ParseError("a hand must have exactly 2 cards, got '" + hand.str() + "'");
  PlayerSpec p;
  p.kind = Kind::Hand;
  p.hand = hand;
  p.label = hand.str();
  return p;
}

PlayerSpec PlayerSpec::from_range(std::string_view range_text) {
  PlayerSpec p;
  p.kind = Kind::Range;
  p.combos = parse_range(range_text);
  p.label = trim(range_text);
  return p;
}

PlayerSpec PlayerSpec::random() {
  PlayerSpec p;
  p.kind = Kind::Random;
  p.label = "random";
  return p;
}

PlayerSpec PlayerSpec::parse(std::string_view text) {
  const std::string t = trim(text);
  std::string low = t;
  std::transform(low.begin(), low.end(), low.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (low.empty() || low == "random" || low == "any" || low == "?" || low == "??" || low == "xx") {
    return random();
  }
  // Exactly two concrete cards -> a known hand. Anything else is range text
  // (which also accepts explicit combos such as "AhKh, AdKd").
  try {
    const CardSet cs = CardSet::parse(t);
    if (cs.size() == 2) return from_hand(cs);
  } catch (const ParseError&) {
    // not a plain card list; fall through to range parsing
  }
  return from_range(t);
}

}  // namespace poker
