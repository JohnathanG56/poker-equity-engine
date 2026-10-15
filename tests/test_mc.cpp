#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/range.hpp"
#include "poker/rng.hpp"

using namespace poker;
using Catch::Approx;

namespace {

std::vector<PlayerSpec> specs(std::initializer_list<const char*> ps) {
  std::vector<PlayerSpec> out;
  for (const char* p : ps) out.push_back(PlayerSpec::parse(p));
  return out;
}

std::vector<CardSet> hands_of(const std::vector<PlayerSpec>& ps) {
  std::vector<CardSet> out;
  for (const auto& p : ps) out.push_back(p.hand);
  return out;
}

/// |mc - truth| <= 3 standard errors for every player.
void check_within_3se(const EquityResult& mc, const std::vector<double>& truth) {
  REQUIRE(mc.players.size() == truth.size());
  for (std::size_t i = 0; i < truth.size(); ++i) {
    const double err = std::abs(mc.players[i].equity - truth[i]);
    INFO("player " << i + 1 << ": mc=" << mc.players[i].equity << " exact=" << truth[i]
                   << " se=" << mc.players[i].std_error);
    CHECK(err <= 3.0 * mc.players[i].std_error + 1e-12);
  }
}

std::vector<double> equities(const EquityResult& r) {
  std::vector<double> out;
  for (const auto& p : r.players) out.push_back(p.equity);
  return out;
}

}  // namespace

TEST_CASE("xoshiro256** matches the reference output", "[rng]") {
  // Reference values produced by an independent Python port of the published
  // algorithm, starting from state {1, 2, 3, 4}.
  Xoshiro256ss rng(std::array<std::uint64_t, 4>{1, 2, 3, 4});
  CHECK(rng.next() == 11520ULL);
  CHECK(rng.next() == 0ULL);
  CHECK(rng.next() == 1509978240ULL);
  CHECK(rng.next() == 1215971899390074240ULL);
  CHECK(rng.next() == 1216172134540287360ULL);
  CHECK(rng.next() == 607988272756665600ULL);
}

TEST_CASE("rng: seeding, jump and bounded draws", "[rng]") {
  Xoshiro256ss a(42), b(42), c(43);
  CHECK(a.next() == b.next());
  CHECK(a.next() != c.next());

  Xoshiro256ss j1(7), j2(7);
  j1.jump();
  j2.jump();
  CHECK(j1.state() == j2.state());          // jump is deterministic
  CHECK(j1.state() != Xoshiro256ss(7).state());

  Xoshiro256ss r(123);
  std::array<int, 52> hist{};
  constexpr int kDraws = 520'000;
  for (int i = 0; i < kDraws; ++i) {
    const auto v = r.bounded(52);
    REQUIRE(v < 52);
    ++hist[v];
  }
  // Loose uniformity check: each bucket expects 10,000 (sd ~ 99).
  for (int h : hist) CHECK(std::abs(h - 10'000) < 600);
  for (int i = 0; i < 1000; ++i) {
    const double u = r.uniform();
    REQUIRE(u >= 0.0);
    REQUIRE(u < 1.0);
  }
}

TEST_CASE("mc: deterministic for the same (seed, threads)", "[mc]") {
  const auto ps = specs({"AsKs", "QQ+, AKo", "random"});
  const CardSet board = CardSet::parse("Jc Tc 2d");
  const auto a = equity_monte_carlo(ps, board, {}, 50'000, 99, 2);
  const auto b = equity_monte_carlo(ps, board, {}, 50'000, 99, 2);
  REQUIRE(a.trials == 50'000);
  CHECK(a.trials == b.trials);
  CHECK(a.rejected == b.rejected);
  for (std::size_t i = 0; i < a.players.size(); ++i) {
    CHECK(a.players[i].equity == b.players[i].equity);  // bit-identical
    CHECK(a.players[i].win == b.players[i].win);
    CHECK(a.players[i].std_error == b.players[i].std_error);
  }
  const auto c = equity_monte_carlo(ps, board, {}, 50'000, 100, 2);
  CHECK(c.players[0].equity != a.players[0].equity);
}

TEST_CASE("mc: equities sum to 1 and SE shrinks like 1/sqrt(N)", "[mc]") {
  const auto ps = specs({"AsKs", "QhQd", "7c6c"});
  const auto small = equity_monte_carlo(ps, {}, {}, 40'000, 1, 1);
  const auto big = equity_monte_carlo(ps, {}, {}, 160'000, 1, 1);
  double sum = 0;
  for (const auto& p : big.players) sum += p.equity;
  CHECK(sum == Approx(1.0).margin(1e-12));
  for (std::size_t i = 0; i < ps.size(); ++i) {
    CHECK(small.players[i].std_error > 0.0);
    const double ratio = big.players[i].std_error / small.players[i].std_error;
    CHECK(ratio == Approx(0.5).margin(0.05));  // 4x samples -> half the error
  }
}

TEST_CASE("mc: fully specified hands agree with exact within 3 SE", "[mc][accuracy]") {
  struct Case {
    std::vector<PlayerSpec> players;
    const char* board;
  };
  const std::vector<Case> cases = {
      {specs({"AsKs", "QhQd"}), ""},
      {specs({"AhAd", "7c2s"}), ""},
      {specs({"AsKs", "QhQd"}), "Jc Tc 2d"},
      {specs({"8h8d", "AcKc", "Js Ts"}), ""},
      {specs({"8h8d", "AcKc", "JsTs"}), "9s 7c 2s"},
      {specs({"AhAd", "KsKc", "QsQc", "JhTh"}), "9h 8h 2c 3d"},
      {specs({"AhAd", "KsKc", "QsQc", "JhTh", "9c9d", "5s4s"}), ""},
  };
  std::uint64_t seed = 1000;
  for (const auto& c : cases) {
    const CardSet board = CardSet::parse(c.board);
    const auto exact = equity_exact(hands_of(c.players), board);
    const auto mc = equity_monte_carlo(c.players, board, {}, 200'000, seed++, 2);
    INFO("board '" << c.board << "', " << c.players.size() << " players");
    check_within_3se(mc, equities(exact));
  }
}

TEST_CASE("mc: a hand vs a range matches the exact range average", "[mc][accuracy]") {
  // AsKs vs QQ: each of the 6 QQ combos is equally likely, so the true
  // equity is the plain average of 6 exact enumerations.
  const auto qq = parse_range("QQ");
  std::vector<double> truth(2, 0.0);
  for (CardSet combo : qq) {
    const auto e = equity_exact({CardSet::parse("AsKs"), combo});
    truth[0] += e.players[0].equity / static_cast<double>(qq.size());
    truth[1] += e.players[1].equity / static_cast<double>(qq.size());
  }
  const auto mc = equity_monte_carlo(specs({"AsKs", "QQ"}), {}, {}, 300'000, 5, 2);
  check_within_3se(mc, truth);
}

TEST_CASE("mc: range vs range with card removal matches exact enumeration", "[mc][accuracy]") {
  // Rejection sampling must produce the uniform distribution over all
  // COMPATIBLE combo pairs. Compute that exactly and compare.
  const CardSet board = CardSet::parse("Jc Tc 2d");
  const auto r1 = remove_conflicts(parse_range("QQ, JJ"), board);
  const auto r2 = remove_conflicts(parse_range("AKs, KQs"), board);
  std::vector<double> truth(2, 0.0);
  int pairs = 0;
  for (CardSet a : r1) {
    for (CardSet b : r2) {
      if (a.intersects(b)) continue;
      const auto e = equity_exact({a, b}, board);
      truth[0] += e.players[0].equity;
      truth[1] += e.players[1].equity;
      ++pairs;
    }
  }
  REQUIRE(pairs > 0);
  for (auto& t : truth) t /= pairs;
  const auto mc = equity_monte_carlo(specs({"QQ, JJ", "AKs, KQs"}), board, {}, 300'000, 6, 2);
  CHECK(mc.rejected > 0);  // KQs conflicts with QQ sometimes
  check_within_3se(mc, truth);
}

TEST_CASE("mc: hand vs random on the flop matches exact enumeration", "[mc][accuracy]") {
  const CardSet board = CardSet::parse("Kc 7h 2s");
  const CardSet hero = CardSet::parse("AsAd");
  const auto villains = remove_conflicts(all_combos(), board | hero);
  std::vector<double> truth(2, 0.0);
  for (CardSet v : villains) {
    const auto e = equity_exact({hero, v}, board);
    truth[0] += e.players[0].equity;
    truth[1] += e.players[1].equity;
  }
  for (auto& t : truth) t /= static_cast<double>(villains.size());
  const auto mc = equity_monte_carlo(specs({"AsAd", "random"}), board, {}, 300'000, 7, 2);
  check_within_3se(mc, truth);
}

TEST_CASE("mc: invalid or impossible input throws", "[mc]") {
  CHECK_THROWS_AS(equity_monte_carlo(specs({"AsKs", "QhQd"}), {}, {}, 0, 1, 1), std::invalid_argument);
  CHECK_THROWS_AS(equity_monte_carlo(specs({"AsKs"}), {}, {}, 10, 1, 1), std::invalid_argument);
  CHECK_THROWS_AS(equity_monte_carlo(specs({"AsKs", "AsQd"}), {}, {}, 10, 1, 1), std::invalid_argument);
  // Every AA combo is blocked by the board
  CHECK_THROWS_AS(equity_monte_carlo(specs({"KsKd", "AA"}), CardSet::parse("As Ah Ad"), {}, 10, 1, 1),
                  std::invalid_argument);
  // Three players cannot all hold AA
  CHECK_THROWS_AS(equity_monte_carlo(specs({"AA", "AA", "AA"}), {}, {}, 10, 1, 1), std::invalid_argument);
}
