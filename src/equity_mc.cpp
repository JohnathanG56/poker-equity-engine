// equity_mc.cpp - Monte Carlo equity for known hands, ranges and random hands.
//
// One sample ("trial"):
//   1. Every player whose cards are not fixed draws a combo uniformly from
//      their (pre-filtered) candidate list. If two players' combos share a
//      card, the WHOLE deal is rejected and redrawn. Rejecting the whole deal
//      (rather than just redrawing the conflicting player) is what makes the
//      result exactly "uniform over all consistent deals"; redrawing one player
//      at a time would bias towards combos that happen to conflict less.
//   2. The missing board cards are drawn uniformly from the cards nobody holds.
//   3. Showdown: evaluate every player, split the pot among the best.
//
// The estimate of a player's equity is the average pot share over all trials.
// Its standard error is sqrt(sample variance / trials), which shrinks like
// 1/sqrt(trials): 100x more samples buys one more decimal digit.
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "equity_common.hpp"
#include "poker/equity.hpp"
#include "poker/evaluator.hpp"
#include "poker/range.hpp"
#include "poker/rng.hpp"

namespace poker {

namespace {

using detail::Tally;

/// Everything a worker needs; read-only once built, shared by all workers.
struct Problem {
  int n = 0;                                        // players
  std::uint64_t fixed = 0;                          // board | dead | known hands
  std::uint64_t board = 0;                          // known board cards
  int missing = 0;                                  // board cards to deal (5 - |board|)
  std::array<std::uint64_t, kMaxPlayers> hole{};    // known hands (0 if variable)
  std::vector<int> variable;                        // players that draw combos
  std::vector<std::vector<std::uint64_t>> combos;   // per variable player
  std::vector<std::uint64_t> deck;                  // cards not in `fixed`
};

/// Integer results of one stream of samples.
struct StreamResult {
  std::array<Tally, kMaxPlayers> tally{};
  std::uint64_t samples = 0;
  std::uint64_t rejected = 0;
};

/// Abort if this many deals in a row are rejected: the ranges are (almost)
/// mutually incompatible, e.g. three players all holding "AA".
constexpr std::uint64_t kMaxConsecutiveRejects = 1'000'000;

/// The hot loop. Pure function of (problem, samples, rng): no shared mutable
/// state, so any number of streams can run concurrently.
StreamResult run_stream(const Problem& pb, std::uint64_t samples, Xoshiro256ss rng) {
  StreamResult out;
  std::array<std::uint64_t, kMaxPlayers> hole = pb.hole;
  std::array<std::uint32_t, kMaxPlayers> strength{};
  const auto deck_size = static_cast<std::uint32_t>(pb.deck.size());
  const std::size_t nvar = pb.variable.size();

  for (std::uint64_t s = 0; s < samples; ++s) {
    std::uint64_t used;
    std::uint64_t consecutive = 0;
    // 1. Deal combos to range / random players, rejecting conflicting deals.
    for (;;) {
      used = pb.fixed;
      bool ok = true;
      for (std::size_t v = 0; v < nvar; ++v) {
        const auto& list = pb.combos[v];
        const std::uint64_t c = list[rng.bounded(static_cast<std::uint32_t>(list.size()))];
        if (c & used) {
          ok = false;
          break;
        }
        used |= c;
        hole[pb.variable[v]] = c;
      }
      if (ok) break;
      ++out.rejected;
      if (++consecutive >= kMaxConsecutiveRejects) {
        throw std::invalid_argument(
            "the players' ranges are incompatible (could not deal a conflict-free hand)");
      }
    }

    // 2. Complete the board from cards nobody holds. `deck` already excludes
    //    the fixed cards, so only cards dealt in step 1 can collide.
    std::uint64_t board = pb.board;
    for (int j = 0; j < pb.missing; ++j) {
      std::uint64_t c;
      do {
        c = pb.deck[rng.bounded(deck_size)];
      } while (c & used);
      used |= c;
      board |= c;
    }

    // 3. Showdown.
    for (int i = 0; i < pb.n; ++i) strength[i] = evaluate7(hole[i] | board);
    detail::score_showdown(strength.data(), pb.n, out.tally.data());
  }
  out.samples = samples;
  return out;
}

Problem build_problem(const std::vector<PlayerSpec>& players, CardSet board, CardSet dead) {
  using namespace detail;
  validate_player_count(players.size());
  validate_board(board);

  Problem pb;
  pb.n = static_cast<int>(players.size());
  CardSet fixed;
  claim_cards(fixed, board, "board");
  claim_cards(fixed, dead, "dead cards");
  for (std::size_t i = 0; i < players.size(); ++i) {
    if (players[i].kind == PlayerSpec::Kind::Hand) {
      if (players[i].hand.size() != 2) {
        throw std::invalid_argument("player " + std::to_string(i + 1) + " must have exactly 2 cards");
      }
      claim_cards(fixed, players[i].hand, "player " + std::to_string(i + 1) + "'s hand");
      pb.hole[i] = players[i].hand.bits();
    }
  }
  pb.fixed = fixed.bits();
  pb.board = board.bits();
  pb.missing = 5 - board.size();

  const std::vector<CardSet> all = all_combos();
  for (std::size_t i = 0; i < players.size(); ++i) {
    const auto& p = players[i];
    if (p.kind == PlayerSpec::Kind::Hand) continue;
    const auto live = remove_conflicts(p.kind == PlayerSpec::Kind::Random ? all : p.combos, fixed);
    if (live.empty()) {
      throw std::invalid_argument("player " + std::to_string(i + 1) + "'s range '" + p.label +
                                  "' has no combos left after removing known cards");
    }
    std::vector<std::uint64_t> masks;
    masks.reserve(live.size());
    for (CardSet c : live) masks.push_back(c.bits());
    pb.variable.push_back(static_cast<int>(i));
    pb.combos.push_back(std::move(masks));
  }

  for (int c = 0; c < kNumCards; ++c) {
    if (!(pb.fixed & Card(c).mask())) pb.deck.push_back(Card(c).mask());
  }
  return pb;
}

}  // namespace

EquityResult equity_monte_carlo(const std::vector<PlayerSpec>& players, CardSet board, CardSet dead,
                                std::uint64_t samples, std::uint64_t seed, unsigned threads) {
  if (samples == 0) throw std::invalid_argument("samples must be positive");
  const Problem pb = build_problem(players, board, dead);

  if (threads == 0) threads = 1;
  if (threads > samples) threads = static_cast<unsigned>(samples);

  // Split samples as evenly as possible: the first (samples % threads)
  // streams get one extra.
  std::vector<std::uint64_t> share(threads, samples / threads);
  for (unsigned t = 0; t < samples % threads; ++t) ++share[t];

  // Stream t uses the base generator jumped t times (2^128 steps each), so
  // streams never overlap and the result depends only on (seed, threads).
  std::vector<Xoshiro256ss> rngs;
  Xoshiro256ss base(seed);
  for (unsigned t = 0; t < threads; ++t) {
    rngs.push_back(base);
    base.jump();
  }

  std::vector<StreamResult> parts(threads);
  for (unsigned t = 0; t < threads; ++t) parts[t] = run_stream(pb, share[t], rngs[t]);

  // Deterministic merge: integer sums, so order cannot change the answer.
  std::array<Tally, kMaxPlayers> total{};
  EquityResult result;
  for (const auto& part : parts) {
    for (int i = 0; i < pb.n; ++i) {
      total[i].wins += part.tally[i].wins;
      total[i].ties += part.tally[i].ties;
      total[i].units += part.tally[i].units;
      total[i].units_sq += part.tally[i].units_sq;
    }
    result.trials += part.samples;
    result.rejected += part.rejected;
  }

  const double N = static_cast<double>(result.trials);
  const double L = static_cast<double>(detail::kShareUnits);
  for (int i = 0; i < pb.n; ++i) {
    PlayerEquity p;
    p.win = static_cast<double>(total[i].wins) / N;
    p.tie = static_cast<double>(total[i].ties) / N;
    p.equity = static_cast<double>(total[i].units) / (N * L);
    // Sample variance of the per-trial share X in [0, 1]:
    //   s^2 = (sum X^2 - N * mean^2) / (N - 1),  SE = sqrt(s^2 / N)
    if (result.trials > 1) {
      const double mean_sq = static_cast<double>(total[i].units_sq) / (N * L * L);
      const double var = std::max(0.0, (mean_sq - p.equity * p.equity) * N / (N - 1.0));
      p.std_error = std::sqrt(var / N);
    }
    result.players.push_back(p);
  }
  return result;
}

}  // namespace poker
