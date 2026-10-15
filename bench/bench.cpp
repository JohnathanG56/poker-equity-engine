// bench - throughput and accuracy benchmarks. Prints Markdown tables.
//
//   ./build/bench            full run (what the README numbers come from)
//   ./build/bench --quick    smaller sizes, for CI smoke tests
//
// Nothing here is hard-coded: every number printed is measured on the machine
// running the binary, and every "exact" reference equity is computed by
// equity_exact() at run time.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include "../tests/reference_eval.hpp"
#include "poker/poker.hpp"
#include "poker/rng.hpp"

using namespace poker;
using Clock = std::chrono::steady_clock;

namespace {

double seconds_since(Clock::time_point t0) {
  return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::string fmt(const char* f, double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, f, v);
  return buf;
}

std::string millions(double per_sec) { return fmt("%.1f M", per_sec / 1e6); }

std::string cpu_name() {
#if defined(__APPLE__)
  char buf[256];
  size_t len = sizeof buf;
  if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) return buf;
#elif defined(__linux__)
  std::ifstream in("/proc/cpuinfo");
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("model name", 0) == 0) {
      const auto colon = line.find(':');
      if (colon != std::string::npos) return line.substr(colon + 2);
    }
  }
#endif
  return "unknown CPU";
}

double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

/// n random 7-card hands as bitmasks (partial Fisher-Yates per hand).
std::vector<std::uint64_t> random_hands(std::size_t n, std::uint64_t seed) {
  Xoshiro256ss rng(seed);
  std::vector<std::uint64_t> out(n);
  std::array<int, 52> deck{};
  std::iota(deck.begin(), deck.end(), 0);
  for (auto& h : out) {
    std::uint64_t m = 0;
    for (int i = 0; i < 7; ++i) {
      const int j = i + static_cast<int>(rng.bounded(static_cast<std::uint32_t>(52 - i)));
      std::swap(deck[i], deck[j]);
      m |= std::uint64_t{1} << deck[i];
    }
    h = m;
  }
  return out;
}

// ---------------------------------------------------------------------------
// (a) evaluator throughput
// ---------------------------------------------------------------------------
void bench_evaluator(std::ostream& out, std::size_t n_hands, int reps) {
  const auto hands = random_hands(n_hands, 12345);

  std::vector<double> rates;
  std::uint64_t checksum = 0;
  for (int r = 0; r < reps; ++r) {
    const auto t0 = Clock::now();
    std::uint64_t sum = 0;
    // Summing every result into a checksum that we print makes the loop's
    // output observable, so the compiler cannot delete the work.
    for (const std::uint64_t h : hands) sum += evaluate7(h);
    const double s = seconds_since(t0);
    rates.push_back(static_cast<double>(n_hands) / s);
    if (r == 0) checksum = sum;
    else if (sum != checksum) std::cerr << "checksum mismatch!\n";
  }

  // The brute-force reference evaluator, for scale (fewer hands: it is slow).
  const std::size_t n_ref = std::min<std::size_t>(n_hands / 20, 500'000);
  std::uint64_t ref_sum = 0;
  const auto t0 = Clock::now();
  for (std::size_t i = 0; i < n_ref; ++i) {
    std::array<Card, 7> cs{};
    int k = 0;
    for (std::uint64_t m = hands[i]; m; m &= m - 1) cs[k++] = Card(std::countr_zero(m));
    ref_sum += reference::eval7(cs);
  }
  const double ref_rate = static_cast<double>(n_ref) / seconds_since(t0);

  out << "## Hand evaluator throughput\n\n";
  out << "`evaluate7` over " << fmt("%.0f", static_cast<double>(n_hands) / 1e6) << "M pre-generated random 7-card hands, single thread, "
      << reps << " repetitions.\n\n";
  out << "| Evaluator | Hands / second | ns / hand |\n|---|---:|---:|\n";
  const double med = median(rates);
  const double best = *std::max_element(rates.begin(), rates.end());
  out << "| `evaluate7` (median of " << reps << ") | " << millions(med) << " | " << fmt("%.2f", 1e9 / med) << " |\n";
  out << "| `evaluate7` (best of " << reps << ") | " << millions(best) << " | " << fmt("%.2f", 1e9 / best) << " |\n";
  out << "| brute-force reference (21 x 5-card subsets) | " << millions(ref_rate) << " | "
      << fmt("%.1f", 1e9 / ref_rate) << " |\n\n";
  out << "Checksum: `" << checksum << "` (reference: `" << ref_sum << "`)\n\n";
}

// ---------------------------------------------------------------------------
// (b) Monte Carlo throughput vs threads, and exact enumeration speed
// ---------------------------------------------------------------------------
void bench_threads(std::ostream& out, std::uint64_t samples, int reps) {
  const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
  std::set<unsigned> counts = {1, 2, hw};
  for (unsigned t : {4u, 8u}) if (t < hw) counts.insert(t);

  struct Scenario {
    const char* name;
    std::vector<PlayerSpec> players;
    const char* board;
  };
  const std::vector<Scenario> scenarios = {
      {"HU preflop, known hands (AsKs vs QhQd)", {PlayerSpec::parse("AsKs"), PlayerSpec::parse("QhQd")}, ""},
      {"3-way preflop, hand vs range vs random",
       {PlayerSpec::parse("AhAd"), PlayerSpec::parse("QQ+, AKs, ATo+, 76s, KQ"), PlayerSpec::random()}, ""},
  };

  out << "## Monte Carlo throughput vs. threads\n\n";
  out << fmt("%.1f", static_cast<double>(samples) / 1e6) << "M samples per run, median of " << reps
      << " runs. `hardware_concurrency()` = " << hw << ".\n\n";
  for (const auto& sc : scenarios) {
    out << "**" << sc.name << "**\n\n";
    out << "| Threads | Samples / second | Speed-up vs 1 thread | Parallel efficiency |\n|---:|---:|---:|---:|\n";
    // Interleave thread counts inside each repetition (1,2,N,1,2,N,...) so
    // that background noise on a shared machine hits every row equally.
    const std::vector<unsigned> tlist(counts.begin(), counts.end());
    std::vector<std::vector<double>> rates(tlist.size());
    for (int r = 0; r < reps; ++r) {
      for (std::size_t k = 0; k < tlist.size(); ++k) {
        const auto t0 = Clock::now();
        const auto res = equity_monte_carlo(sc.players, CardSet::parse(sc.board), {}, samples,
                                            static_cast<std::uint64_t>(r + 1), tlist[k]);
        rates[k].push_back(static_cast<double>(res.trials) / seconds_since(t0));
      }
    }
    double base = 0;
    for (std::size_t k = 0; k < tlist.size(); ++k) {
      const unsigned t = tlist[k];
      const double rate = median(rates[k]);
      if (t == 1) base = rate;
      out << "| " << t << " | " << millions(rate) << " | " << fmt("%.2fx", rate / base) << " | "
          << fmt("%.0f%%", 100.0 * rate / base / t) << " |\n";
    }
    out << "\n";
  }

  // Exact enumeration speed for reference.
  out << "**Exact enumeration (single thread)**\n\n| Matchup | Boards | Time | Boards / second |\n|---|---:|---:|---:|\n";
  struct ExactCase {
    const char* name;
    std::vector<const char*> hands;
  };
  for (const ExactCase& ec : {ExactCase{"HU preflop AsKs vs QhQd", {"AsKs", "QhQd"}},
                              ExactCase{"6-way preflop", {"AhAd", "KsKc", "QsQc", "JhTh", "9c9d", "5s4s"}}}) {
    std::vector<CardSet> hs;
    for (const char* h : ec.hands) hs.push_back(CardSet::parse(h));
    const auto t0 = Clock::now();
    const auto r = equity_exact(hs);
    const double s = seconds_since(t0);
    out << "| " << ec.name << " | " << r.trials << " | " << fmt("%.3f s", s) << " | "
        << millions(static_cast<double>(r.trials) / s) << " |\n";
  }
  out << "\n";
}

// ---------------------------------------------------------------------------
// (c) accuracy: Monte Carlo vs exact on fully specified matchups
// ---------------------------------------------------------------------------
void bench_accuracy(std::ostream& out, std::uint64_t samples) {
  struct Matchup {
    std::vector<const char*> hands;
    const char* board;
  };
  const std::vector<Matchup> matchups = {
      // heads-up preflop
      {{"AsKs", "QhQd"}, ""},
      {{"AhAd", "KsKc"}, ""},
      {{"AhKd", "7s7c"}, ""},
      {{"AsKd", "AhQc"}, ""},
      {{"JhTh", "AcKd"}, ""},
      {{"8s8d", "9c8c"}, ""},
      {{"Ac2d", "KhQh"}, ""},
      {{"5h4h", "AsAc"}, ""},
      // heads-up flop
      {{"AsKs", "QhQd"}, "Jc Tc 2d"},
      {{"AhAd", "Ts9s"}, "8s 7h 2s"},
      {{"KcQc", "AhJd"}, "Kd 7c 2c"},
      {{"6h6d", "AcKc"}, "6c Ts 2h"},
      {{"9h8h", "AdAc"}, "7h 6s 2h"},
      {{"QsJs", "TcTd"}, "Ks 9d 3s"},
      // heads-up turn
      {{"AsKs", "QhQd"}, "Jc Tc 2d 7h"},
      {{"Ah5h", "KdKc"}, "Qh 8h 2c 3s"},
      // 3-way preflop
      {{"AsKs", "QhQd", "7c6c"}, ""},
      {{"AhAd", "KsKc", "JhTh"}, ""},
      {{"8s8d", "AcQd", "Kh9h"}, ""},
      {{"2c2d", "AhKh", "QsJs"}, ""},
      // 3-way flop
      {{"AsKs", "QhQd", "7c6c"}, "Jc Tc 2d"},
      {{"AhAd", "KsKc", "JhTh"}, "9h 8h 2c"},
      {{"8s8d", "AcQd", "Kh9h"}, "Kd 8c 4h"},
      // multiway
      {{"AhAd", "KsKc", "QsQc", "JhTh"}, ""},
      {{"AhAd", "KsKc", "QsQc", "JhTh"}, "9h 8h 2c"},
      {{"AcKc", "7d7h", "QsJs", "9c8c", "2h2s"}, ""},
      {{"AhAd", "KsKc", "QsQc", "JhTh", "9c9d", "5s4s"}, ""},
      {{"AhAd", "KsKc", "QsQc", "JhTh", "9c9d", "5s4s"}, "Ts 7s 3d"},
  };

  out << "## Accuracy: Monte Carlo vs. exact enumeration\n\n";
  out << matchups.size() << " fully specified matchups (heads-up, 3-way and 4-6 way; preflop, flop and turn). "
      << "Each runs " << samples / 1'000'000.0 << "M Monte Carlo samples (all hardware threads, seed = matchup #) "
      << "and is compared with the exact equity from full enumeration. "
      << "Error is |MC - exact| in percentage points (pp); z = error / reported standard error.\n\n";
  out << "| # | Hands | Board | Exact equity % | MC equity % | Max error (pp) | Max \\|z\\| |\n";
  out << "|---:|---|---|---|---|---:|---:|\n";

  double max_err = 0, sum_err = 0;
  int n_err = 0, within1 = 0, within2 = 0, within3 = 0;
  int idx = 0;
  for (const auto& m : matchups) {
    ++idx;
    std::vector<CardSet> hands;
    std::vector<PlayerSpec> specs;
    std::string hand_txt;
    for (const char* h : m.hands) {
      hands.push_back(CardSet::parse(h));
      specs.push_back(PlayerSpec::from_hand(CardSet::parse(h)));
      hand_txt += (hand_txt.empty() ? "" : " vs ") + std::string(h);
    }
    const CardSet board = CardSet::parse(m.board);
    const auto ex = equity_exact(hands, board);
    const auto mc = equity_monte_carlo(specs, board, {}, samples, static_cast<std::uint64_t>(idx), 0);
    std::string ex_txt, mc_txt;
    double row_max = 0, row_z = 0;
    for (std::size_t i = 0; i < hands.size(); ++i) {
      const double err = std::abs(mc.players[i].equity - ex.players[i].equity) * 100.0;
      const double z = mc.players[i].std_error > 0 ? err / (100.0 * mc.players[i].std_error) : 0.0;
      row_max = std::max(row_max, err);
      row_z = std::max(row_z, z);
      max_err = std::max(max_err, err);
      sum_err += err;
      ++n_err;
      within1 += z <= 1.0;
      within2 += z <= 2.0;
      within3 += z <= 3.0;
      ex_txt += (i ? " / " : "") + fmt("%.2f", 100.0 * ex.players[i].equity);
      mc_txt += (i ? " / " : "") + fmt("%.2f", 100.0 * mc.players[i].equity);
    }
    out << "| " << idx << " | " << hand_txt << " | " << (board.empty() ? "-" : board.str()) << " | " << ex_txt
        << " | " << mc_txt << " | " << fmt("%.3f", row_max) << " | " << fmt("%.2f", row_z) << " |\n";
  }
  out << "\n**Summary over " << matchups.size() << " matchups / " << n_err << " player equities:**\n\n";
  out << "| Metric | Value |\n|---|---:|\n";
  out << "| Max absolute error | " << fmt("%.3f", max_err) << " pp |\n";
  out << "| Mean absolute error | " << fmt("%.3f", sum_err / n_err) << " pp |\n";
  out << "| Within 1 SE (expect ~68%) | " << fmt("%.0f%%", 100.0 * within1 / n_err) << " |\n";
  out << "| Within 2 SE (expect ~95%) | " << fmt("%.0f%%", 100.0 * within2 / n_err) << " |\n";
  out << "| Within 3 SE (expect ~99.7%) | " << fmt("%.0f%%", 100.0 * within3 / n_err) << " |\n\n";
}

}  // namespace

int main(int argc, char** argv) {
  bool quick = false;
  std::string out_path;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quick") == 0) quick = true;
    else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
    else {
      std::cerr << "usage: bench [--quick] [--out FILE.md]\n";
      return 2;
    }
  }

  std::ostringstream md;
  md << "# Benchmark results\n\n";
  md << "- CPU: " << cpu_name() << "\n";
  md << "- Hardware threads: " << std::thread::hardware_concurrency() << "\n";
  double load[3] = {0, 0, 0};
  if (getloadavg(load, 3) == 3) {
    // A load average near or above the core count means other processes were
    // competing for the CPU and throughput numbers will be noisy / low.
    md << "- Load average at start (1/5/15 min): " << fmt("%.2f", load[0]) << " / " << fmt("%.2f", load[1])
       << " / " << fmt("%.2f", load[2]) << "\n";
  }
#if defined(__clang__)
  md << "- Compiler: clang " << __clang_major__ << "." << __clang_minor__ << "\n";
#elif defined(__GNUC__)
  md << "- Compiler: GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "\n";
#endif
#ifdef NDEBUG
  md << "- Build: optimized (NDEBUG)\n";
#else
  md << "- Build: DEBUG - numbers are not meaningful, rebuild with -DCMAKE_BUILD_TYPE=Release\n";
#endif
  md << "- Mode: " << (quick ? "--quick (reduced sizes)" : "full") << "\n\n";
  std::cout << md.str() << std::flush;

  auto emit = [&](auto&& section) {
    std::ostringstream s;
    section(s);
    std::cout << s.str() << std::flush;
    md << s.str();
  };
  emit([&](std::ostream& o) { bench_evaluator(o, quick ? 1'000'000 : 10'000'000, quick ? 3 : 5); });
  emit([&](std::ostream& o) { bench_threads(o, quick ? 500'000 : 5'000'000, quick ? 3 : 5); });
  emit([&](std::ostream& o) { bench_accuracy(o, quick ? 100'000 : 1'000'000); });

  if (!out_path.empty()) {
    std::ofstream f(out_path);
    f << md.str();
    std::cerr << "wrote " << out_path << "\n";
  }
  return 0;
}
