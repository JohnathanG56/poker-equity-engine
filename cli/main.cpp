// equity - command-line front end for the Poker Equity Engine.
//
//   ./build/equity "AsKs" "QhQd" --board "Jc Tc 2d" --samples 1000000 --threads 4
//   ./build/equity "AsKs" "QhQd" --board "Jc Tc 2d" --exact
//   ./build/equity "AhAd" "QQ+, AKs" random --samples 2000000
//   ./build/equity --eval "As Ks Qs Js Ts 2d 3c"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "poker/poker.hpp"

namespace {

constexpr const char* kUsage = R"(Poker Equity Engine - Texas Hold'em all-in equity calculator

Usage:
  equity [options] PLAYER PLAYER [PLAYER ...]     (2 to 9 players)
  equity --eval "CARDS"                           (rank a 5-7 card hand)

PLAYER is one of:
  a hand          "AsKs", "As Ks", "Td9d"
  a range         "QQ+, AKs, ATo+, 76s, KQ", "22-55", "A2s-A5s"
  random          an unknown hand ("random", "any" or "?")

Options:
  -b, --board CARDS    known community cards: 0, 3, 4 or 5 (e.g. "Jc Tc 2d")
  -d, --dead CARDS     cards known to be out of the deck
  -e, --exact          enumerate every board exactly (all players must be hands)
  -n, --samples N      Monte Carlo samples (default 1000000; accepts 1e6)
  -t, --threads N      worker threads (default: all hardware threads)
  -s, --seed N         RNG seed (default 1); same seed + threads = same result
      --json           print machine-readable JSON instead of the table
      --eval CARDS     print the best 5-card hand made from 5-7 cards
  -h, --help           show this help
)";

struct Options {
  std::vector<std::string> players;
  std::string board;
  std::string dead;
  std::string eval;
  bool exact = false;
  bool json = false;
  bool samples_given = false;
  std::uint64_t samples = 1'000'000;
  unsigned threads = 0;
  std::uint64_t seed = 1;
};

[[noreturn]] void usage_error(const std::string& msg) {
  std::cerr << "error: " << msg << "\n\nRun 'equity --help' for usage.\n";
  std::exit(2);
}

std::uint64_t parse_count(const std::string& flag, const std::string& v) {
  try {
    std::size_t pos = 0;
    if (v.find_first_of("eE.") != std::string::npos) {
      const double d = std::stod(v, &pos);
      if (pos != v.size() || d < 0 || d != std::floor(d) || d > 1e15) throw std::invalid_argument(v);
      return static_cast<std::uint64_t>(d);
    }
    const unsigned long long n = std::stoull(v, &pos);
    if (pos != v.size()) throw std::invalid_argument(v);
    return n;
  } catch (const std::exception&) {
    usage_error(flag + " expects a non-negative integer, got '" + v + "'");
  }
}

Options parse_args(int argc, char** argv) {
  Options o;
  auto value = [&](int& i, const std::string& flag) -> std::string {
    if (i + 1 >= argc) usage_error(flag + " needs a value");
    return argv[++i];
  };
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      std::cout << kUsage;
      std::exit(0);
    } else if (a == "-b" || a == "--board") {
      o.board = value(i, a);
    } else if (a == "-d" || a == "--dead") {
      o.dead = value(i, a);
    } else if (a == "-e" || a == "--exact") {
      o.exact = true;
    } else if (a == "-n" || a == "--samples") {
      o.samples = parse_count(a, value(i, a));
      o.samples_given = true;
    } else if (a == "-t" || a == "--threads") {
      o.threads = static_cast<unsigned>(parse_count(a, value(i, a)));
    } else if (a == "-s" || a == "--seed") {
      o.seed = parse_count(a, value(i, a));
    } else if (a == "--json") {
      o.json = true;
    } else if (a == "--eval") {
      o.eval = value(i, a);
    } else if (a.size() > 1 && a[0] == '-' && a != "-") {
      usage_error("unknown option '" + a + "'");
    } else {
      o.players.push_back(a);
    }
  }
  return o;
}

std::string with_commas(std::uint64_t n) {
  std::string s = std::to_string(n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
  return s;
}

std::string pct(double x, int decimals = 2) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, 100.0 * x);
  return buf;
}

std::string pad(std::string s, std::size_t w, bool right = false) {
  if (s.size() >= w) return s;
  return right ? std::string(w - s.size(), ' ') + s : s + std::string(w - s.size(), ' ');
}

std::string json_string(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

std::string num(double x) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.17g", x);
  return buf;
}

std::string bar(double equity, int width = 24) {
  const int full = static_cast<int>(std::lround(equity * width));
  std::string out;
  for (int i = 0; i < width; ++i) out += i < full ? "█" : "░";
  return out;
}

int run_eval(const std::string& text) {
  const poker::CardSet cards = poker::CardSet::parse(text);
  if (cards.size() < 5 || cards.size() > 7) {
    std::cerr << "error: --eval needs 5 to 7 cards, got " << cards.size() << "\n";
    return 1;
  }
  const std::uint32_t v = poker::evaluate(cards);
  std::cout << cards.str() << "\n  " << poker::describe(v) << "\n  strength = " << v << " (category "
            << (v >> poker::kCategoryShift) << ")\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const Options o = parse_args(argc, argv);
  try {
    if (!o.eval.empty()) return run_eval(o.eval);
    if (o.players.size() < 2) usage_error("need at least 2 players");

    const poker::CardSet board = poker::CardSet::parse(o.board);
    const poker::CardSet dead = poker::CardSet::parse(o.dead);
    std::vector<poker::PlayerSpec> specs;
    for (const auto& p : o.players) specs.push_back(poker::PlayerSpec::parse(p));

    bool all_known = true;
    poker::CardSet fixed = board | dead;
    for (const auto& s : specs) {
      if (s.kind == poker::PlayerSpec::Kind::Hand) fixed |= s.hand;
      else all_known = false;
    }
    if (o.exact && !all_known) {
      std::cerr << "error: --exact needs every player to be a specific hand; ranges and random\n"
                   "       hands need Monte Carlo (drop --exact).\n";
      return 1;
    }
    const unsigned threads = o.threads ? o.threads : std::max(1u, std::thread::hardware_concurrency());

    const auto t0 = std::chrono::steady_clock::now();
    poker::EquityResult r;
    if (o.exact) {
      std::vector<poker::CardSet> hands;
      for (const auto& s : specs) hands.push_back(s.hand);
      r = poker::equity_exact(hands, board, dead);
    } else {
      r = poker::equity_monte_carlo(specs, board, dead, o.samples, o.seed, threads);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    auto combos_of = [&](const poker::PlayerSpec& s) -> std::size_t {
      if (s.kind == poker::PlayerSpec::Kind::Range) return poker::remove_conflicts(s.combos, fixed).size();
      if (s.kind == poker::PlayerSpec::Kind::Random) return poker::remove_conflicts(poker::all_combos(), fixed).size();
      return 1;
    };

    if (o.json) {
      std::cout << "{\"mode\": " << (r.exact ? "\"exact\"" : "\"monte_carlo\"") << ", \"board\": "
                << json_string(board.str()) << ", \"trials\": " << r.trials << ", \"rejected\": " << r.rejected
                << ", \"threads\": " << (r.exact ? 1 : threads) << ", \"seed\": " << o.seed
                << ", \"seconds\": " << num(secs) << ", \"players\": [";
      for (std::size_t i = 0; i < specs.size(); ++i) {
        const auto& p = r.players[i];
        std::cout << (i ? ", " : "") << "{\"label\": " << json_string(specs[i].label)
                  << ", \"combos\": " << combos_of(specs[i]) << ", \"win\": " << num(p.win)
                  << ", \"tie\": " << num(p.tie) << ", \"equity\": " << num(p.equity)
                  << ", \"std_error\": " << num(p.std_error) << "}";
      }
      std::cout << "]}\n";
      return 0;
    }

    // ---- header -------------------------------------------------------------
    std::cout << "\n  Board    " << (board.empty() ? "(preflop)" : board.str()) << "\n";
    if (!dead.empty()) std::cout << "  Dead     " << dead.str() << "\n";
    if (r.exact) {
      std::cout << "  Mode     exact enumeration: " << with_commas(r.trials) << " boards\n";
    } else {
      std::cout << "  Mode     Monte Carlo: " << with_commas(r.trials) << " samples, " << threads
                << (threads == 1 ? " thread" : " threads") << ", seed " << o.seed << "\n";
    }
    char tbuf[96];
    if (r.exact) {
      std::snprintf(tbuf, sizeof tbuf, "%.3f s", secs);
    } else {
      std::snprintf(tbuf, sizeof tbuf, "%.3f s (%.2fM samples/s)", secs,
                    static_cast<double>(r.trials) / secs / 1e6);
    }
    std::cout << "  Time     " << tbuf << "\n\n";

    // ---- table --------------------------------------------------------------
    const bool show_se = !r.exact;
    std::cout << "  " << pad("#", 3) << pad("Player", 24) << pad("Combos", 8, true) << pad("Win %", 9, true)
              << pad("Tie %", 8, true) << pad("Equity %", 10, true) << (show_se ? pad("+/- SE", 9, true) : "")
              << "   \n";
    std::cout << "  " << std::string(3 + 24 + 8 + 9 + 8 + 10 + (show_se ? 9 : 0), '-') << "\n";
    for (std::size_t i = 0; i < specs.size(); ++i) {
      const auto& s = specs[i];
      const auto& p = r.players[i];
      const std::size_t combos = combos_of(s);
      std::string label = s.label;
      if (label.size() > 22) label = label.substr(0, 21) + "~";
      std::cout << "  " << pad(std::to_string(i + 1), 3) << pad(label, 24) << pad(std::to_string(combos), 8, true)
                << pad(pct(p.win), 9, true) << pad(pct(p.tie), 8, true) << pad(pct(p.equity), 10, true)
                << (show_se ? pad(pct(p.std_error, 3), 9, true) : "") << "   " << bar(p.equity) << "\n";
    }
    // Current made hands, when there is a flop and the hand is known.
    if (board.size() >= 3) {
      std::cout << "\n";
      for (std::size_t i = 0; i < specs.size(); ++i) {
        if (specs[i].kind != poker::PlayerSpec::Kind::Hand) continue;
        std::cout << "  " << (i + 1) << ": " << poker::describe(poker::evaluate(specs[i].hand | board)) << "\n";
      }
    }
    if (!r.exact && all_known) {
      std::cout << "\n  tip: every hand is known - add --exact for a zero-error answer.\n";
    }
    std::cout << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
