// card.hpp - Card and CardSet: the two value types everything else is built on.
//
// Encoding
// --------
// A card is an integer in [0, 52):
//
//     index = rank * 4 + suit
//
//   rank: 0 = '2', 1 = '3', ..., 8 = 'T', 9 = 'J', 10 = 'Q', 11 = 'K', 12 = 'A'
//   suit: 0 = 'c', 1 = 'd', 2 = 'h', 3 = 's'
//
// So 2c = 0, 2d = 1, 2h = 2, 2s = 3, 3c = 4, ..., As = 51.
//
// A set of cards is a 64-bit integer where bit `index` is set when that card is
// present (only the low 52 bits are ever used). Set operations become single
// instructions: union is `|`, intersection is `&`, size is popcount.
//
// The rank-major layout (rank * 4 + suit) means each rank occupies one 4-bit
// "nibble" of the mask: bits [4r, 4r+3] hold the four suits of rank r. The
// evaluator (evaluator.hpp) exploits that layout heavily.
#pragma once

#include <bit>
#include <compare>
#include <initializer_list>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace poker {

/// Thrown for any malformed card / board / range text, or duplicate cards.
class ParseError : public std::invalid_argument {
 public:
  using std::invalid_argument::invalid_argument;
};

inline constexpr int kNumRanks = 13;
inline constexpr int kNumSuits = 4;
inline constexpr int kNumCards = 52;

/// Rank characters indexed by rank number ("23456789TJQKA").
inline constexpr std::string_view kRankChars = "23456789TJQKA";
/// Suit characters indexed by suit number ("cdhs").
inline constexpr std::string_view kSuitChars = "cdhs";

/// Returns the rank number for a rank character ('2'..'9', 'T','J','Q','K','A',
/// case-insensitive), or -1 if the character is not a rank.
int rank_from_char(char c) noexcept;
/// Returns the suit number for 'c','d','h','s' (case-insensitive), or -1.
int suit_from_char(char c) noexcept;

class Card {
 public:
  constexpr Card() = default;
  /// Construct from an index in [0, 52). Not range-checked (hot path).
  constexpr explicit Card(int index) : idx_(static_cast<std::uint8_t>(index)) {}
  /// Construct from rank in [0, 13) and suit in [0, 4).
  constexpr Card(int rank, int suit) : idx_(static_cast<std::uint8_t>(rank * 4 + suit)) {}

  constexpr int index() const noexcept { return idx_; }
  constexpr int rank() const noexcept { return idx_ >> 2; }  // index / 4
  constexpr int suit() const noexcept { return idx_ & 3; }   // index % 4
  /// The single-bit mask for this card.
  constexpr std::uint64_t mask() const noexcept { return std::uint64_t{1} << idx_; }

  /// Parse exactly one card such as "As", "Td", "7c". Throws ParseError.
  static Card parse(std::string_view text);
  /// Two-character representation, e.g. "As".
  std::string str() const;

  constexpr auto operator<=>(const Card&) const = default;

 private:
  std::uint8_t idx_ = 0;
};

/// A set of distinct cards stored as a 52-bit mask inside a uint64_t.
class CardSet {
 public:
  constexpr CardSet() = default;
  constexpr explicit CardSet(std::uint64_t bits) : bits_(bits) {}
  constexpr CardSet(std::initializer_list<Card> cards) {
    for (Card c : cards) bits_ |= c.mask();
  }

  constexpr std::uint64_t bits() const noexcept { return bits_; }
  constexpr int size() const noexcept { return std::popcount(bits_); }
  constexpr bool empty() const noexcept { return bits_ == 0; }
  constexpr bool contains(Card c) const noexcept { return (bits_ & c.mask()) != 0; }
  constexpr bool intersects(CardSet o) const noexcept { return (bits_ & o.bits_) != 0; }

  constexpr void add(Card c) noexcept { bits_ |= c.mask(); }
  constexpr void remove(Card c) noexcept { bits_ &= ~c.mask(); }

  constexpr CardSet operator|(CardSet o) const noexcept { return CardSet{bits_ | o.bits_}; }
  constexpr CardSet operator&(CardSet o) const noexcept { return CardSet{bits_ & o.bits_}; }
  constexpr CardSet& operator|=(CardSet o) noexcept { bits_ |= o.bits_; return *this; }
  constexpr bool operator==(const CardSet&) const = default;

  /// The cards in ascending index order.
  std::vector<Card> cards() const;

  /// Parse a list of cards: "AsKs", "As Ks", "As,Ks" and "Jc Tc 2d" all work.
  /// Throws ParseError on malformed input or a duplicated card.
  static CardSet parse(std::string_view text);
  /// Cards in descending rank order separated by spaces, e.g. "As Kd 7c".
  std::string str() const;

 private:
  std::uint64_t bits_ = 0;
};

/// The full 52-card deck mask.
inline constexpr std::uint64_t kFullDeck = (std::uint64_t{1} << 52) - 1;

}  // namespace poker
