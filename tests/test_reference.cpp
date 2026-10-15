// Cross-checks the fast evaluator against the brute-force reference evaluator
// and against exhaustive enumeration of every 5- and 7-card hand.
#include "catch_amalgamated.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <numeric>
#include <random>
#include <vector>

#include "poker/card.hpp"
#include "poker/evaluator.hpp"
#include "reference_eval.hpp"

using namespace poker;

namespace {

std::array<Card, 7> random_hand(std::mt19937_64& rng) {
  std::array<int, 52> deck{};
  std::iota(deck.begin(), deck.end(), 0);
  // Partial Fisher-Yates: the first 7 entries become a uniform random hand.
  std::array<Card, 7> hand{};
  for (int i = 0; i < 7; ++i) {
    std::uniform_int_distribution<int> pick(i, 51);
    std::swap(deck[i], deck[pick(rng)]);
    hand[i] = Card(deck[i]);
  }
  return hand;
}

std::uint64_t to_mask(const std::array<Card, 7>& hand) {
  std::uint64_t m = 0;
  for (Card c : hand) m |= c.mask();
  return m;
}

}  // namespace

TEST_CASE("evaluate7 matches the brute-force reference on 1,000,000 random hands",
          "[evaluator][reference]") {
  constexpr int kHands = 1'000'000;
  std::mt19937_64 rng(20240607);  // fixed seed: reproducible

  std::vector<std::uint32_t> fast(kHands);
  std::vector<std::uint64_t> slow(kHands);
  int category_mismatches = 0;
  for (int i = 0; i < kHands; ++i) {
    const auto hand = random_hand(rng);
    fast[i] = evaluate7(to_mask(hand));
    slow[i] = reference::eval7(hand);
    if (static_cast<int>(category_of(fast[i])) != reference::category(slow[i])) {
      ++category_mismatches;
    }
  }
  REQUIRE(category_mismatches == 0);

  // Same ordering and same ties: sort every hand by the reference value, then
  // walk the sorted order. Wherever the reference says "equal", the fast
  // values must be equal; wherever it says "strictly greater", they must be
  // strictly greater. That proves the two evaluators induce the identical
  // total preorder on all 1M hands (not just on adjacent pairs).
  std::vector<int> order(kHands);
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return slow[a] < slow[b]; });
  int ordering_errors = 0;
  for (int k = 1; k < kHands; ++k) {
    const int a = order[k - 1];
    const int b = order[k];
    const bool ref_equal = slow[a] == slow[b];
    const bool ok = ref_equal ? fast[a] == fast[b] : fast[a] < fast[b];
    if (!ok) ++ordering_errors;
  }
  REQUIRE(ordering_errors == 0);
}

TEST_CASE("evaluate7 matches the reference on every 5-card hand", "[evaluator][exhaustive]") {
  // All C(52,5) = 2,598,960 hands. The category frequencies are textbook
  // combinatorics (e.g. 40 straight flushes, 624 quads), and there are 7,462
  // distinct hand values in 5-card poker.
  std::map<std::uint64_t, std::uint32_t> ref_to_fast;  // reference value -> fast value
  std::array<std::uint64_t, 9> counts{};
  std::uint64_t total = 0;
  bool consistent = true;
  for (int a = 0; a < 52; ++a)
    for (int b = a + 1; b < 52; ++b)
      for (int c = b + 1; c < 52; ++c)
        for (int d = c + 1; d < 52; ++d)
          for (int e = d + 1; e < 52; ++e) {
            const std::array<Card, 5> h{Card(a), Card(b), Card(c), Card(d), Card(e)};
            const std::uint64_t mask = h[0].mask() | h[1].mask() | h[2].mask() | h[3].mask() | h[4].mask();
            const std::uint32_t f = evaluate7(mask);
            const std::uint64_t r = reference::eval5(h);
            auto [it, inserted] = ref_to_fast.emplace(r, f);
            if (!inserted && it->second != f) consistent = false;
            ++counts[static_cast<std::size_t>(category_of(f))];
            ++total;
          }
  REQUIRE(consistent);
  REQUIRE(total == 2'598'960);
  REQUIRE(ref_to_fast.size() == 7462);
  // The map is ordered by reference value; fast values must be strictly
  // increasing in the same order.
  std::uint32_t prev = 0;
  bool first = true;
  for (const auto& [ref, f] : ref_to_fast) {
    if (!first) REQUIRE(f > prev);
    prev = f;
    first = false;
  }
  CHECK(counts[0] == 1'302'540);  // high card
  CHECK(counts[1] == 1'098'240);  // pair
  CHECK(counts[2] == 123'552);    // two pair
  CHECK(counts[3] == 54'912);     // trips
  CHECK(counts[4] == 10'200);     // straight
  CHECK(counts[5] == 5'108);      // flush
  CHECK(counts[6] == 3'744);      // full house
  CHECK(counts[7] == 624);        // quads
  CHECK(counts[8] == 40);         // straight flush (incl. royal)
}

TEST_CASE("exhaustive 7-card category frequencies", "[evaluator][exhaustive]") {
  // All C(52,7) = 133,784,560 seven-card hands. Known results: 4,824 distinct
  // best-hand values, and the category frequencies below.
  std::array<std::uint64_t, 9> counts{};
  // One bit per possible strength value (< 2^30): 128 MB of bits, used to
  // count distinct values without a slow hash set.
  std::vector<bool> seen(std::size_t{1} << 30, false);
  std::uint64_t distinct = 0;
  std::uint64_t total = 0;
  for (int a = 0; a < 52; ++a) {
    const std::uint64_t ma = 1ULL << a;
    for (int b = a + 1; b < 52; ++b) {
      const std::uint64_t mb = ma | (1ULL << b);
      for (int c = b + 1; c < 52; ++c) {
        const std::uint64_t mc = mb | (1ULL << c);
        for (int d = c + 1; d < 52; ++d) {
          const std::uint64_t md = mc | (1ULL << d);
          for (int e = d + 1; e < 52; ++e) {
            const std::uint64_t me = md | (1ULL << e);
            for (int f = e + 1; f < 52; ++f) {
              const std::uint64_t mf = me | (1ULL << f);
              for (int g = f + 1; g < 52; ++g) {
                const std::uint32_t v = evaluate7(mf | (1ULL << g));
                ++counts[v >> kCategoryShift];
                if (!seen[v]) {
                  seen[v] = true;
                  ++distinct;
                }
                ++total;
              }
            }
          }
        }
      }
    }
  }
  REQUIRE(total == 133'784'560);
  CHECK(counts[0] == 23'294'460);
  CHECK(counts[1] == 58'627'800);
  CHECK(counts[2] == 31'433'400);
  CHECK(counts[3] == 6'461'620);
  CHECK(counts[4] == 6'180'020);
  CHECK(counts[5] == 4'047'644);
  CHECK(counts[6] == 3'473'184);
  CHECK(counts[7] == 224'848);
  CHECK(counts[8] == 41'584);
  CHECK(distinct == 4824);
}
