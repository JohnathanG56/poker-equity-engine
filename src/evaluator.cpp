#include "poker/evaluator.hpp"

#include <string>

namespace poker {

namespace {

constexpr const char* kRankNames[13] = {"Two",  "Three", "Four", "Five",  "Six",  "Seven", "Eight",
                                        "Nine", "Ten",   "Jack", "Queen", "King", "Ace"};
constexpr const char* kRankPlurals[13] = {"Twos",  "Threes", "Fours", "Fives",  "Sixes",
                                          "Sevens", "Eights", "Nines", "Tens",  "Jacks",
                                          "Queens", "Kings",  "Aces"};

std::string name(int r) { return kRankNames[r]; }
std::string plural(int r) { return kRankPlurals[r]; }

}  // namespace

const char* category_name(HandCategory c) noexcept {
  switch (c) {
    case HandCategory::HighCard: return "High Card";
    case HandCategory::Pair: return "Pair";
    case HandCategory::TwoPair: return "Two Pair";
    case HandCategory::ThreeOfAKind: return "Three of a Kind";
    case HandCategory::Straight: return "Straight";
    case HandCategory::Flush: return "Flush";
    case HandCategory::FullHouse: return "Full House";
    case HandCategory::FourOfAKind: return "Four of a Kind";
    case HandCategory::StraightFlush: return "Straight Flush";
  }
  return "Unknown";
}

std::string describe(std::uint32_t strength) {
  using detail::top_bit;
  const HandCategory cat = category_of(strength);
  const std::uint32_t major = (strength >> kMajorShift) & 0x1FFFu;
  const std::uint32_t minor = strength & 0x1FFFu;
  const int hi = major ? top_bit(major) : 0;
  switch (cat) {
    case HandCategory::HighCard: return "High Card, " + name(hi);
    case HandCategory::Pair: return "Pair of " + plural(hi);
    case HandCategory::TwoPair: {
      const int lo = top_bit(major & ~(1u << hi));
      return "Two Pair, " + plural(hi) + " and " + plural(lo) +
             (minor ? " (" + name(top_bit(minor)) + " kicker)" : "");
    }
    case HandCategory::ThreeOfAKind: return "Three of a Kind, " + plural(hi);
    case HandCategory::Straight: return "Straight, " + name(hi) + " high";
    case HandCategory::Flush: return "Flush, " + name(hi) + " high";
    case HandCategory::FullHouse:
      return "Full House, " + plural(hi) + " full of " + plural(top_bit(minor));
    case HandCategory::FourOfAKind: return "Four of a Kind, " + plural(hi);
    case HandCategory::StraightFlush:
      return hi == 12 ? std::string("Royal Flush") : "Straight Flush, " + name(hi) + " high";
  }
  return "Unknown";
}

}  // namespace poker
