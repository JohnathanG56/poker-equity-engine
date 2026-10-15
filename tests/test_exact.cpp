#include "catch_amalgamated.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "reference_eval.hpp"

using namespace poker;
using Catch::Approx;

namespace {

std::vector<CardSet> hands(std::initializer_list<const char*> hs) {
  std::vector<CardSet> out;
  for (const char* h : hs) out.push_back(CardSet::parse(h));
  return out;
}

double equity_sum(const EquityResult& r) {
  double s = 0;
  for (const auto& p : r.players) s += p.equity;
  return s;
}

std::uint64_t choose(int n, int k) {
  std::uint64_t r = 1;
  for (int i = 1; i <= k; ++i) r = r * static_cast<std::uint64_t>(n - k + i) / static_cast<std::uint64_t>(i);
  return r;
}

}  // namespace

TEST_CASE("exact: equities sum to 1 and board counts are C(n, k)", "[exact]") {
  struct Case {
    std::vector<CardSet> players;
    const char* board;
  };
  const std::vector<Case> cases = {
      {hands({"AsKs", "QhQd"}), ""},
      {hands({"AsKs", "QhQd"}), "Jc Tc 2d"},
      {hands({"AsKs", "QhQd"}), "Jc Tc 2d 7h"},
      {hands({"AsKs", "QhQd"}), "Jc Tc 2d 7h 3s"},
      {hands({"AsKs", "QhQd", "7c6c"}), ""},
      {hands({"AsKs", "QhQd", "7c6c"}), "Jc Tc 2d"},
      {hands({"AhAd", "KsKc", "QsQc", "JhTh"}), "9h 8h 2c"},
      {hands({"AhAd", "KsKc", "QsQc", "JhTh", "9c9d", "5s4s"}), ""},
  };
  for (const auto& c : cases) {
    const CardSet board = CardSet::parse(c.board);
    const auto r = equity_exact(c.players, board);
    INFO("board '" << c.board << "', players " << c.players.size());
    CHECK(r.exact);
    CHECK(equity_sum(r) == Approx(1.0).margin(1e-12));
    const int remaining = 52 - 2 * static_cast<int>(c.players.size()) - board.size();
    CHECK(r.trials == choose(remaining, 5 - board.size()));
    for (const auto& p : r.players) {
      CHECK(p.win >= 0.0);
      CHECK(p.tie >= 0.0);
      CHECK(p.equity >= p.win);              // ties only add equity
      CHECK(p.equity <= p.win + p.tie + 1e-12);  // a split is worth less than a whole pot
      CHECK(p.std_error == 0.0);
    }
  }
}

TEST_CASE("exact: suit-symmetric hands split exactly", "[exact]") {
  // AhKd vs AdKh: swapping the heart and diamond suits maps each hand onto the
  // other and leaves the deck unchanged, so the equities must be identical.
  const auto r = equity_exact(hands({"AhKd", "AdKh"}));
  CHECK(r.players[0].equity == Approx(0.5).margin(1e-12));
  CHECK(r.players[1].equity == Approx(0.5).margin(1e-12));
  CHECK(r.players[0].win == Approx(r.players[1].win).margin(1e-12));
  CHECK(r.players[0].tie == Approx(r.players[1].tie).margin(1e-12));
}

TEST_CASE("exact: board that plays for everyone is a full split", "[exact]") {
  const auto r = equity_exact(hands({"2c3d", "4h5c", "7d8c"}), CardSet::parse("As Ks Qs Js Ts"));
  REQUIRE(r.trials == 1);
  for (const auto& p : r.players) {
    CHECK(p.win == 0.0);
    CHECK(p.tie == 1.0);
    CHECK(p.equity == Approx(1.0 / 3.0).margin(1e-12));
  }
}

TEST_CASE("exact: river with a clear winner", "[exact]") {
  const auto r = equity_exact(hands({"AsAd", "KsKd"}), CardSet::parse("Ac 7h 2c 9d 4s"));
  REQUIRE(r.trials == 1);
  CHECK(r.players[0].equity == 1.0);
  CHECK(r.players[0].win == 1.0);
  CHECK(r.players[1].equity == 0.0);
}

TEST_CASE("exact: drawing dead on the turn", "[exact]") {
  // Quads on the board for player 1 vs a hand that can't catch up.
  const auto r = equity_exact(hands({"9s9h", "2c3d"}), CardSet::parse("9c 9d Kh 7c"));
  CHECK(r.trials == 44);
  CHECK(r.players[0].equity == 1.0);
  CHECK(r.players[1].equity == 0.0);
}

namespace {

// Independent brute force: reference evaluator, floating-point shares, and a
// straightforward double loop over the (at most two) missing board cards.
std::vector<double> brute_force(const std::vector<CardSet>& players, CardSet board, CardSet dead,
                                std::uint64_t* boards_out) {
  CardSet used = board | dead;
  for (auto h : players) used |= h;
  const int n = static_cast<int>(players.size());
  const int k = 5 - board.size();
  REQUIRE(k <= 2);
  std::vector<double> eq(static_cast<std::size_t>(n), 0.0);
  std::uint64_t boards = 0;
  auto showdown = [&](CardSet full) {
    ++boards;
    std::vector<std::uint64_t> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      const auto cs = (players[i] | full).cards();
      std::array<Card, 7> seven{};
      std::copy(cs.begin(), cs.end(), seven.begin());
      v[i] = reference::eval7(seven);
    }
    const auto best = *std::max_element(v.begin(), v.end());
    const auto winners = static_cast<double>(std::count(v.begin(), v.end(), best));
    for (int i = 0; i < n; ++i) if (v[i] == best) eq[i] += 1.0 / winners;
  };
  for (int a = 0; a < 52; ++a) {
    if (used.contains(Card(a))) continue;
    if (k == 1) {
      showdown(board | CardSet{Card(a).mask()});
      continue;
    }
    for (int b = a + 1; b < 52; ++b) {
      if (used.contains(Card(b))) continue;
      showdown(board | CardSet{Card(a).mask() | Card(b).mask()});
    }
  }
  for (auto& e : eq) e /= static_cast<double>(boards);
  *boards_out = boards;
  return eq;
}

}  // namespace

TEST_CASE("exact: matches an independent brute force (turn and flop)", "[exact]") {
  const auto players = hands({"AsKs", "QhQd", "8c8d"});
  for (const char* b : {"Jc Tc 2d 7h", "Jc Tc 2d"}) {
    const CardSet board = CardSet::parse(b);
    const auto r = equity_exact(players, board);
    std::uint64_t boards = 0;
    const auto eq = brute_force(players, board, {}, &boards);
    INFO("board " << b);
    REQUIRE(r.trials == boards);
    for (int i = 0; i < 3; ++i) CHECK(r.players[i].equity == Approx(eq[i]).margin(1e-12));
  }
}

TEST_CASE("exact: player order does not matter", "[exact]") {
  const auto a = equity_exact(hands({"AsKs", "QhQd", "7c6c"}), CardSet::parse("Jc Tc 2d"));
  const auto b = equity_exact(hands({"7c6c", "AsKs", "QhQd"}), CardSet::parse("Jc Tc 2d"));
  CHECK(a.players[0].equity == Approx(b.players[1].equity).margin(1e-12));
  CHECK(a.players[1].equity == Approx(b.players[2].equity).margin(1e-12));
  CHECK(a.players[2].equity == Approx(b.players[0].equity).margin(1e-12));
}

TEST_CASE("exact: dead cards are removed from the deck", "[exact]") {
  const auto players = hands({"AsKs", "QhQd"});
  const CardSet board = CardSet::parse("Jc Tc 2d");
  const CardSet dead = CardSet::parse("Qc Qs");
  const auto live = equity_exact(players, board);
  const auto r = equity_exact(players, board, dead);
  CHECK(live.trials == choose(45, 2));
  CHECK(r.trials == choose(43, 2));
  std::uint64_t boards = 0;
  const auto eq = brute_force(players, board, dead, &boards);
  REQUIRE(r.trials == boards);
  for (int i = 0; i < 2; ++i) CHECK(r.players[i].equity == Approx(eq[i]).margin(1e-12));
  // Killing the queens removes AK's straight outs AND QQ's set outs; the net
  // effect is not obvious by intuition, which is exactly why we enumerate.
  CHECK(r.players[0].equity != Approx(live.players[0].equity).margin(1e-6));
}

TEST_CASE("exact: sanity on lopsided matchups", "[exact]") {
  // Bounds, not memorised values: an overpair is a big favourite over a hand
  // with no pair and no overcards; a dominated hand is a big underdog.
  const auto aa = equity_exact(hands({"AhAd", "7c2s"}));
  CHECK(aa.players[0].equity > 0.8);
  const auto dom = equity_exact(hands({"AsKd", "AhQc"}));
  CHECK(dom.players[0].equity > 0.65);
  CHECK(dom.players[0].tie > 0.0);  // plenty of chopped boards share an Ace
}

TEST_CASE("exact: invalid input throws", "[exact]") {
  CHECK_THROWS_AS(equity_exact(hands({"AsKs"})), std::invalid_argument);
  CHECK_THROWS_AS(equity_exact(hands({"AsKs", "AsQd"})), std::invalid_argument);
  CHECK_THROWS_AS(equity_exact(hands({"AsKs", "QhQd"}), CardSet::parse("Jc")), std::invalid_argument);
  CHECK_THROWS_AS(equity_exact(hands({"AsKs", "QhQd"}), CardSet::parse("Jc Tc As")), std::invalid_argument);
  CHECK_THROWS_AS(equity_exact(hands({"AsKsQs", "QhQd"})), std::invalid_argument);
  CHECK_THROWS_AS(equity_exact(hands({"AsKs", "QhQd"}), {}, CardSet::parse("Qh")), std::invalid_argument);
  std::vector<CardSet> ten;
  for (int i = 0; i < 10; ++i) ten.push_back(CardSet{Card(2 * i).mask() | Card(2 * i + 1).mask()});
  CHECK_THROWS_AS(equity_exact(ten), std::invalid_argument);
}
