// rng.hpp - Fast, high-quality, reproducible random numbers.
//
// xoshiro256** (Blackman & Vigna, 2018): 256 bits of state, period 2^256 - 1,
// passes BigCrush, and costs a handful of shifts/xors/multiplies per 64-bit
// output. Far better than std::rand (tiny state, implementation-defined) and
// cheaper than std::mt19937_64 (2.5 KB of state).
//
// Seeding: a 64-bit seed is expanded into the 256-bit state with SplitMix64,
// the generator the xoshiro authors recommend for this (it never produces the
// all-zero state that would make xoshiro output zeros forever).
//
// Parallel streams: jump() advances the generator by 2^128 steps. Thread t
// starts from the base state jumped t times, so no two threads can ever
// produce overlapping sequences.
#pragma once

#include <array>
#include <cstdint>

namespace poker {

class SplitMix64 {
 public:
  constexpr explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}
  constexpr std::uint64_t next() noexcept {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

 private:
  std::uint64_t state_;
};

class Xoshiro256ss {
 public:
  using result_type = std::uint64_t;

  constexpr explicit Xoshiro256ss(std::uint64_t seed) noexcept {
    SplitMix64 sm(seed);
    for (auto& word : s_) word = sm.next();
  }
  /// Construct from an explicit state (must not be all zero).
  constexpr explicit Xoshiro256ss(const std::array<std::uint64_t, 4>& state) noexcept : s_(state) {}

  static constexpr std::uint64_t min() noexcept { return 0; }
  static constexpr std::uint64_t max() noexcept { return ~std::uint64_t{0}; }

  constexpr std::uint64_t next() noexcept {
    const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }
  constexpr std::uint64_t operator()() noexcept { return next(); }

  /// Uniform integer in [0, n) for 0 < n < 2^32, with no modulo bias.
  ///
  /// Lemire's method: multiply a random 32-bit x by n to get a 64-bit product;
  /// the high 32 bits are floor(x * n / 2^32), which is in [0, n). A few x
  /// values would make some outputs one-in-2^32 more likely; those are exactly
  /// the cases where the low 32 bits fall below (2^32 mod n), so we reject and
  /// redraw only then. The expensive `%` runs only on that rare slow path.
  constexpr std::uint32_t bounded(std::uint32_t n) noexcept {
    std::uint64_t m = (next() >> 32) * n;
    auto low = static_cast<std::uint32_t>(m);
    if (low < n) {
      const std::uint32_t threshold = static_cast<std::uint32_t>(-n) % n;  // 2^32 mod n
      while (low < threshold) {
        m = (next() >> 32) * n;
        low = static_cast<std::uint32_t>(m);
      }
    }
    return static_cast<std::uint32_t>(m >> 32);
  }

  /// Uniform double in [0, 1) using the top 53 bits.
  constexpr double uniform() noexcept { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

  /// Advance the state by 2^128 calls to next(): used to give each thread a
  /// non-overlapping subsequence.
  constexpr void jump() noexcept {
    constexpr std::uint64_t kJump[] = {0x180EC6D33CFD0ABAULL, 0xD5A61266F0C9392CULL,
                                       0xA9582618E03FC9AAULL, 0x39ABDC4529B1661CULL};
    std::array<std::uint64_t, 4> acc{};
    for (std::uint64_t word : kJump) {
      for (int b = 0; b < 64; ++b) {
        if (word & (std::uint64_t{1} << b)) {
          for (int i = 0; i < 4; ++i) acc[i] ^= s_[i];
        }
        next();
      }
    }
    s_ = acc;
  }

  constexpr const std::array<std::uint64_t, 4>& state() const noexcept { return s_; }

 private:
  static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
  }
  std::array<std::uint64_t, 4> s_{};
};

}  // namespace poker
