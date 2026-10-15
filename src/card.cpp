#include "poker/card.hpp"

#include <algorithm>
#include <string>

namespace poker {

int rank_from_char(char c) noexcept {
  switch (c) {
    case '2': return 0;
    case '3': return 1;
    case '4': return 2;
    case '5': return 3;
    case '6': return 4;
    case '7': return 5;
    case '8': return 6;
    case '9': return 7;
    case 'T': case 't': return 8;
    case 'J': case 'j': return 9;
    case 'Q': case 'q': return 10;
    case 'K': case 'k': return 11;
    case 'A': case 'a': return 12;
    default: return -1;
  }
}

int suit_from_char(char c) noexcept {
  switch (c) {
    case 'c': case 'C': return 0;
    case 'd': case 'D': return 1;
    case 'h': case 'H': return 2;
    case 's': case 'S': return 3;
    default: return -1;
  }
}

Card Card::parse(std::string_view text) {
  if (text.size() != 2) {
    throw ParseError("invalid card '" + std::string(text) + "': expected 2 characters like 'As'");
  }
  const int r = rank_from_char(text[0]);
  const int s = suit_from_char(text[1]);
  if (r < 0) throw ParseError("invalid rank '" + std::string(1, text[0]) + "' in card '" + std::string(text) + "'");
  if (s < 0) throw ParseError("invalid suit '" + std::string(1, text[1]) + "' in card '" + std::string(text) + "'");
  return Card(r, s);
}

std::string Card::str() const {
  return {kRankChars[static_cast<std::size_t>(rank())], kSuitChars[static_cast<std::size_t>(suit())]};
}

std::vector<Card> CardSet::cards() const {
  std::vector<Card> out;
  out.reserve(static_cast<std::size_t>(size()));
  // Iterate set bits: countr_zero gives the index of the lowest set bit, and
  // m &= m - 1 clears it.
  for (std::uint64_t m = bits_; m != 0; m &= m - 1) out.emplace_back(std::countr_zero(m));
  return out;
}

CardSet CardSet::parse(std::string_view text) {
  CardSet set;
  std::size_t i = 0;
  auto is_sep = [](char c) { return c == ' ' || c == ',' || c == '\t' || c == '\n'; };
  while (i < text.size()) {
    if (is_sep(text[i])) {
      ++i;
      continue;
    }
    if (i + 1 >= text.size() || is_sep(text[i + 1])) {
      throw ParseError("invalid card list '" + std::string(text) + "': dangling character '" +
                       std::string(1, text[i]) + "'");
    }
    const Card c = Card::parse(text.substr(i, 2));
    if (set.contains(c)) {
      throw ParseError("duplicate card " + c.str() + " in '" + std::string(text) + "'");
    }
    set.add(c);
    i += 2;
  }
  return set;
}

std::string CardSet::str() const {
  std::vector<Card> cs = cards();
  // Highest rank first (then by suit), which is how players read hands.
  std::sort(cs.begin(), cs.end(), [](Card a, Card b) { return a.index() > b.index(); });
  std::string out;
  for (std::size_t k = 0; k < cs.size(); ++k) {
    if (k) out += ' ';
    out += cs[k].str();
  }
  return out;
}

}  // namespace poker
