#include "poker/card.hpp"

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

}  // namespace poker
