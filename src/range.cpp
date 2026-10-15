// range.cpp - parse standard hand-range notation into concrete combos.
#include "poker/range.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace poker {

namespace {

enum class Suitedness { Any, Suited, Offsuit };

/// A "hand class" such as QQ, AKs, AKo or AK (r1 >= r2).
struct HandClass {
  int r1 = 0;
  int r2 = 0;
  Suitedness suit = Suitedness::Any;
  bool is_pair() const { return r1 == r2; }
};

CardSet combo(int r1, int s1, int r2, int s2) {
  return CardSet{Card(r1, s1).mask() | Card(r2, s2).mask()};
}

void add_class(std::vector<CardSet>& out, const HandClass& hc) {
  if (hc.is_pair()) {
    // C(4,2) = 6 ways to pick two of the four suits.
    for (int s1 = 0; s1 < 4; ++s1)
      for (int s2 = s1 + 1; s2 < 4; ++s2) out.push_back(combo(hc.r1, s1, hc.r1, s2));
    return;
  }
  for (int s1 = 0; s1 < 4; ++s1) {
    for (int s2 = 0; s2 < 4; ++s2) {
      const bool suited = s1 == s2;
      if (hc.suit == Suitedness::Suited && !suited) continue;
      if (hc.suit == Suitedness::Offsuit && suited) continue;
      out.push_back(combo(hc.r1, s1, hc.r2, s2));
    }
  }
}

[[noreturn]] void fail(std::string_view token, const std::string& why) {
  throw ParseError("invalid range token '" + std::string(token) + "': " + why);
}

HandClass parse_class(std::string_view tok, std::string_view whole_token) {
  if (tok.size() != 2 && tok.size() != 3) fail(whole_token, "expected something like QQ, AKs, AKo or AK");
  const int a = rank_from_char(tok[0]);
  const int b = rank_from_char(tok[1]);
  if (a < 0 || b < 0) fail(whole_token, "unknown rank character");
  HandClass hc;
  hc.r1 = std::max(a, b);
  hc.r2 = std::min(a, b);
  if (tok.size() == 3) {
    const char s = static_cast<char>(std::tolower(static_cast<unsigned char>(tok[2])));
    if (s == 's') hc.suit = Suitedness::Suited;
    else if (s == 'o') hc.suit = Suitedness::Offsuit;
    else fail(whole_token, "suffix must be 's' (suited) or 'o' (offsuit)");
    if (hc.is_pair()) fail(whole_token, "a pocket pair cannot be suited/offsuit");
  }
  return hc;
}

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

void parse_token(std::string_view tok, std::vector<CardSet>& out) {
  const std::string low = lower(tok);
  if (low == "random" || low == "any" || low == "100%" || low == "all") {
    const auto all = all_combos();
    out.insert(out.end(), all.begin(), all.end());
    return;
  }

  // Explicit combo, e.g. "AhKh".
  if (tok.size() == 4 && rank_from_char(tok[0]) >= 0 && suit_from_char(tok[1]) >= 0 &&
      rank_from_char(tok[2]) >= 0 && suit_from_char(tok[3]) >= 0) {
    const CardSet cs = CardSet::parse(tok);  // throws on "AhAh"
    out.push_back(cs);
    return;
  }

  // Range between two classes, e.g. "22-55" or "A2s-A5s".
  if (const auto dash = tok.find('-'); dash != std::string_view::npos) {
    const HandClass lo = parse_class(tok.substr(0, dash), tok);
    const HandClass hi = parse_class(tok.substr(dash + 1), tok);
    if (lo.is_pair() && hi.is_pair()) {
      for (int r = std::min(lo.r1, hi.r1); r <= std::max(lo.r1, hi.r1); ++r) add_class(out, {r, r, Suitedness::Any});
      return;
    }
    if (lo.is_pair() || hi.is_pair()) fail(tok, "cannot mix a pair and a non-pair in a dash range");
    if (lo.r1 != hi.r1) fail(tok, "both ends of a dash range must share the top card (e.g. A2s-A5s)");
    if (lo.suit != hi.suit) fail(tok, "both ends of a dash range must have the same suitedness");
    for (int r = std::min(lo.r2, hi.r2); r <= std::max(lo.r2, hi.r2); ++r) add_class(out, {lo.r1, r, lo.suit});
    return;
  }

  // "QQ+" or "ATo+".
  if (!tok.empty() && tok.back() == '+') {
    const HandClass hc = parse_class(tok.substr(0, tok.size() - 1), tok);
    if (hc.is_pair()) {
      for (int r = hc.r1; r <= 12; ++r) add_class(out, {r, r, Suitedness::Any});  // up to AA
    } else {
      for (int r = hc.r2; r < hc.r1; ++r) add_class(out, {hc.r1, r, hc.suit});  // kicker climbs
    }
    return;
  }

  add_class(out, parse_class(tok, tok));
}

}  // namespace

std::vector<CardSet> all_combos() {
  std::vector<CardSet> out;
  out.reserve(1326);
  for (int a = 0; a < kNumCards; ++a)
    for (int b = a + 1; b < kNumCards; ++b) out.push_back(CardSet{Card(a).mask() | Card(b).mask()});
  return out;
}

std::vector<CardSet> parse_range(std::string_view text) {
  std::vector<CardSet> out;
  std::size_t i = 0;
  bool any_token = false;
  while (i < text.size()) {
    const char c = text[i];
    if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }
    std::size_t j = i;
    while (j < text.size() && text[j] != ',' && !std::isspace(static_cast<unsigned char>(text[j]))) ++j;
    parse_token(text.substr(i, j - i), out);
    any_token = true;
    i = j;
  }
  if (!any_token) throw ParseError("empty range");
  std::sort(out.begin(), out.end(), [](CardSet a, CardSet b) { return a.bits() < b.bits(); });
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<CardSet> remove_conflicts(const std::vector<CardSet>& combos, CardSet blocked) {
  std::vector<CardSet> out;
  out.reserve(combos.size());
  for (CardSet c : combos) {
    if (!c.intersects(blocked)) out.push_back(c);
  }
  return out;
}

}  // namespace poker
