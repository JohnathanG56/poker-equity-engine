# Poker Equity Engine

A Texas Hold'em **equity engine in modern C++20**: a branch-light 7-card hand evaluator built on
bit tricks and compile-time lookup tables, **exact** equity by enumerating every board,
**multithreaded Monte Carlo** for hand ranges and unknown hands (with standard errors), a
command-line tool, a benchmark suite, and a **WebAssembly** build that powers an in-browser
calculator running in a Web Worker.

```
$ ./build/equity "AsKs" "QhQd" --board "Jc Tc 2d" --exact

  Board    Jc Tc 2d
  Mode     exact enumeration: 990 boards

  #  Player                    Combos    Win %   Tie %  Equity %
  --------------------------------------------------------------
  1  As Ks                          1    28.69    0.00     28.69   ███████░░░░░░░░░░░░░░░░░
  2  Qh Qd                          1    71.31    0.00     71.31   █████████████████░░░░░░░

  1: High Card, Ace
  2: Pair of Queens
```

```
$ ./build/equity AhAd "QQ+, AKs, ATo+, 76s, KQ" random --samples 1000000 --threads 2

  Board    (preflop)
  Mode     Monte Carlo: 1,000,000 samples, 2 threads, seed 1

  #  Player                    Combos    Win %   Tie %  Equity %   +/- SE
  -----------------------------------------------------------------------
  1  Ah Ad                          1    72.67    2.08     73.66    0.043   ██████████████████░░░░░░
  2  QQ+, AKs, ATo+, 76s, ~        59    11.36    2.17     12.39    0.032   ███░░░░░░░░░░░░░░░░░░░░░
  3  random                      1225    13.72    0.58     13.95    0.034   ███░░░░░░░░░░░░░░░░░░░░░
```

(The CLI also prints a `Time` line with throughput; it is omitted above because it depends on the machine.)

---

## Features

- **7-card evaluator** (`evaluate7`): one `uint64_t` card mask in, one `uint32_t` strength out;
  higher wins, equal ties. Per-suit rank masks via a portable "pext" bit gather, flush by popcount,
  straights by shift-and-AND (including the A-2-3-4-5 wheel), pairs/trips/quads with a bit-sliced
  adder. Two 8192-entry tables (16 KB each) are generated **at compile time** (`constexpr`), so
  there is no startup cost and the whole engine is ~108 KB of WebAssembly.
- **Verified exhaustively**: agrees with a deliberately naive 21-subset reference evaluator on
  1,000,000 seeded random hands (identical ordering *and* ties), on all 2,598,960 five-card hands
  (7,462 distinct values), and reproduces the category frequencies of all 133,784,560 seven-card
  hands (4,824 distinct values).
- **Exact equity** for 2–9 known hands, any 0/3/4/5-card board, and dead cards. Split pots are
  accumulated as integers in units of 1/2520 of a pot (lcm(1..9)), so ties are exact.
- **Monte Carlo equity** where each player is a known hand, a **range** (`QQ+, AKs, ATo+, 76s, KQ,
  22-55, A2s-A5s, AhKh`) or **random**. Whole-deal rejection sampling keeps range-vs-range deals
  uniform over *compatible* combos; every equity comes with a **standard error**.
- **Multithreaded** with `std::thread`: per-thread xoshiro256** streams (jump-ahead, never
  overlapping), stack-local integer tallies, no locks or atomics in the hot loop, deterministic
  merge — the same `(seed, threads)` always gives bit-identical results.
- **CLI** (`equity`) with exact/MC modes, `--json` output, and `--eval` for single hands.
- **Benchmarks** (`bench`) that print Markdown: evaluator throughput, MC throughput vs threads,
  exact-enumeration speed, and MC-vs-exact accuracy over 28 matchups.
- **WebAssembly + web UI**: Embind bindings, a 52-card picker, 2–6 players (cards / range /
  random), live equity bars; exact when all hands are known, progressive Monte Carlo otherwise.
  All computation happens in a module Web Worker, so the page never blocks.
- **Tests**: 51 Catch2 test cases (unit, property and exhaustive), a Node smoke test that checks
  the wasm build is **bit-for-bit identical** to the native CLI, and CI on Linux (GCC, Clang),
  macOS, ASan/UBSan, TSan and Emscripten.

## Architecture

```
                     ┌───────────────────────────── include/poker/ ─────────────────────────────┐
                     │                                                                          │
  "As Ks", "QQ+,AKs" │  card.hpp      Card (0..51 = rank*4+suit), CardSet (52-bit mask), parsing │
  ───────────────────┼─▶range.hpp     range text ──▶ list of 2-card combos                       │
                     │  evaluator.hpp evaluate7(mask) ──▶ strength  [constexpr tables]           │
                     │  rng.hpp       SplitMix64 seeding, xoshiro256**, jump(), bounded()        │
                     │  equity.hpp    PlayerSpec, equity_exact, equity_monte_carlo               │
                     └───────────────┬──────────────────────────────────────────┬──────────────┘
                                     │                                          │
                 src/equity_exact.cpp│                          src/equity_mc.cpp│
                 enumerate C(n,k)    │                 split samples across T workers
                 board completions   │      ┌──────────┬──────────┬──────────┐ │
                       │             │      │ thread 0 │ thread 1 │ thread T │ (rng jumped t times,
                       ▼             │      │ deal→eval│ deal→eval│ deal→eval│  local integer tallies)
                 score_showdown ◀────┴──────┴────┬─────┴──────────┴──────────┘
                 (integer pot shares)            │ join + integer merge (deterministic)
                                                 ▼
                                           EquityResult {win, tie, equity, std_error}
                                                 │
            ┌────────────────────────┬───────────┴──────────────┬──────────────────────────┐
            ▼                        ▼                          ▼                          ▼
      cli/main.cpp             bench/bench.cpp            tests/*.cpp           web/bindings/poker_wasm.cpp
      ./build/equity           ./build/bench              Catch2 + reference          (Embind, Emscripten)
                                                          evaluator                         │
                                                                                            ▼
                                                                web/worker.js (Web Worker) ◀─▶ web/app.js (UI)
```

## How the evaluator works (short version)

Cards are bits `rank*4 + suit` in a 64-bit mask, so each rank owns one 4-bit nibble.

1. **Per-suit rank masks.** `(cards >> s) & 0x1111…1` keeps suit `s` (one bit per nibble);
   a 4-step shift/OR/mask "gather" packs those 13 bits together (a portable `pext`).
2. **Flush**: a suit mask with `popcount >= 5`.
3. **Straights**: `m & m>>1 & m>>2 & m>>3 & m>>4` is non-zero exactly where five ranks in a row
   start; a copy of the Ace at the bottom handles the wheel. Precomputed for all 8192 masks.
4. **Rank multiplicities**: add the four suit masks as 1-bit numbers with a tiny **bit-sliced
   adder** (half adders), giving count bits `b2 b1 b0` for all 13 ranks at once:
   quads = `b2`, trips = `b1 & b0`, pairs = `b1 & ~b0`.
5. **Encode** `category (4 bits) | major ranks (13-bit mask) | kickers (13-bit mask)`. Masks
   with equal popcount compare lexicographically, so one integer comparison settles any showdown,
   kickers included.

[LEARN.md](LEARN.md) walks through a full example hand bit by bit.

## Exact vs. Monte Carlo vs. ranges

| Situation | Engine | Why |
|---|---|---|
| Every hole card known | `equity_exact` | Only the board is unknown: enumerate all `C(remaining, 5 - board)` completions (1,712,304 heads-up preflop; 990 on a flop). Zero error. |
| Any player is a range or random | `equity_monte_carlo` | The space is (hands × boards) — e.g. 1,225 villain hands × 1.7M boards per hand — so sample deals instead and report a standard error that shrinks like 1/√N. |

**Ranges.** Text is parsed to concrete combos (`QQ` = 6, `AKs` = 4, `AKo` = 12, `AK` = 16),
combos that use a known card are removed, and each sample draws one combo per player uniformly.
If two players' combos collide, the **whole deal** is rejected and redrawn, which makes every
*compatible* assignment equally likely (redrawing just one player would bias the result). The
test suite proves this by comparing against exact range-vs-range enumeration.

**Standard error.** Each sample gives a player a pot share `X ∈ {0, 1/k, …, 1}`. The engine keeps
`ΣX` and `ΣX²` as integers, so `SE = sqrt(sample variance / N)`. A 95% interval is roughly
equity ± 2 SE.

## Build, test, benchmark (macOS, Apple Silicon)

```bash
# prerequisites
xcode-select --install          # Apple clang (C++20)
brew install cmake

# build + test (Catch2 is fetched automatically)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

# use it
./build/equity "AsKs" "QhQd" --board "Jc Tc 2d" --samples 1000000 --threads 4
./build/equity "AsKs" "QhQd" --board "Jc Tc 2d" --exact
./build/equity AhAd "QQ+, AKs" random --json
./build/equity --eval "As Ks Qs Js Ts 2d 3c"

# benchmark (prints Markdown; --out also writes it to a file)
./build/bench --out docs/bench-results-mac.md
```

Linux is the same with `apt install cmake g++`. On x86-64 the build enables the hardware
`POPCNT` instruction (`-DPOKER_X86_POPCNT=OFF` to disable); `-DPOKER_NATIVE_ARCH=ON` adds
`-march=native`.

### Web demo (WebAssembly)

```bash
brew install emscripten            # or: emsdk install latest && emsdk activate latest
./scripts/build-wasm.sh            # -> web/wasm/poker.js + poker.wasm
node web/test/smoke.mjs build/equity   # checks wasm against the native CLI
python3 -m http.server -d web 8000     # open http://localhost:8000
```

The page must be served over HTTP (module workers do not load from `file://`). The wasm build
was verified with Emscripten 3.1.51. **GitHub Pages:** push to `main`, then in the repo settings
choose *Pages → Source: GitHub Actions*; `.github/workflows/pages.yml` builds the wasm and deploys
`web/`.

## Resume numbers (fill from your machine)

Run on your own laptop (plugged in, other apps closed), Release build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/bench --out docs/bench-results-mac.md
```

| Metric | Your machine | Where in the output |
|---|---|---|
| `evaluate7` throughput (median) | ___ M hands/s | "Hand evaluator throughput" |
| MC samples/s, 1 thread (HU preflop) | ___ M/s | "Monte Carlo throughput vs. threads" |
| MC samples/s, all threads | ___ M/s (___x) | same table, last row |
| Exact HU preflop enumeration | ___ s for 1,712,304 boards | "Exact enumeration" |
| MC accuracy (1M samples, 28 matchups) | max ___ pp, mean ___ pp | "Accuracy" summary |
| Tests | 51 test cases pass | `ctest --test-dir build` |

CPU / cores: ________________

### Reference results from the development container

> **Measured in a 2-core Linux cloud dev container — rerun `./build/bench` on your own machine for resume numbers.**
> Full output: [docs/bench-results-cloud.md](docs/bench-results-cloud.md). The container was shared
> with other workloads, so throughput numbers are noisy.

| Metric | Cloud container (Intel Xeon @ 2.80 GHz, 2 vCPU, GCC 13.3) |
|---|---|
| `evaluate7` | 57.8 M hands/s median (17.3 ns/hand), single thread |
| Brute-force reference evaluator | 0.4 M hands/s (for scale) |
| MC, HU preflop known hands | 15.4 M samples/s (1 thread), 29.5 M samples/s (2 threads, 1.92x) |
| MC, hand vs range vs random | 10.1 M samples/s (1 thread), 19.8 M samples/s (2 threads, 1.96x) |
| Exact HU preflop (1,712,304 boards) | 0.063 s |
| MC vs exact, 28 matchups × 1M samples | max abs error 0.126 pp, mean 0.035 pp; 100% of 78 equities within 3 SE |

## Project layout

```
include/poker/   public headers: card, evaluator, range, rng, equity (+ poker.hpp umbrella)
src/             card parsing, evaluator descriptions, exact + Monte Carlo engines, ranges
tests/           Catch2 tests + reference_eval.hpp (slow, obviously-correct evaluator)
bench/           benchmark binary (Markdown output)
cli/             the `equity` command-line tool
web/             Embind bindings, Web Worker, UI (index.html / app.js / style.css), Node smoke test
scripts/         build-wasm.sh
docs/            benchmark results
.github/         CI (Linux/macOS, sanitizers, wasm) and GitHub Pages deployment
```

## Limitations / next steps

- Ranges are unweighted (every combo equally likely); no "50% of AKo" weights yet.
- The WebAssembly build is single-threaded (no `SharedArrayBuffer`/pthreads); it relies on the
  Worker for responsiveness rather than parallelism.
- Exact enumeration is single-threaded; it is already sub-second for typical spots.
- Faster evaluators exist (e.g. large perfect-hash tables); this one deliberately trades a few
  nanoseconds for tiny tables that fit in WebAssembly. See LEARN.md for ideas.

## License

MIT — see [LICENSE](LICENSE).
