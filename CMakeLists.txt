cmake_minimum_required(VERSION 3.20)
project(poker_equity_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to an optimized build: this is a performance project.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

option(POKER_BUILD_TESTS "Build unit tests" ON)
option(POKER_BUILD_BENCH "Build the benchmark binary" ON)
option(POKER_BUILD_CLI   "Build the command-line tool" ON)
option(POKER_NATIVE_ARCH "Compile with -march=native (not portable)" OFF)
option(POKER_X86_POPCNT  "On x86-64, use the hardware POPCNT instruction (any CPU since ~2008)" ON)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(poker
  src/card.cpp
  src/evaluator.cpp
  src/equity_exact.cpp
  src/equity_mc.cpp
  src/player_spec.cpp
  src/range.cpp
)
target_include_directories(poker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(poker PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
if(POKER_NATIVE_ARCH)
  target_compile_options(poker PUBLIC -march=native)
elseif(POKER_X86_POPCNT AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND NOT EMSCRIPTEN)
  # Baseline x86-64 has no popcount instruction, so std::popcount would fall
  # back to a ~12-instruction bit-twiddling sequence. The evaluator calls it
  # several times per hand; the hardware instruction is ~1.5x faster overall.
  # (ARM64 / Apple Silicon and WebAssembly have a native popcount already.)
  target_compile_options(poker PUBLIC -mpopcnt)
endif()

# ---------------------------------------------------------------------------
# WebAssembly build (emcmake cmake ...): library + Embind bindings only.
# Produces web/wasm/poker.js (ES module) and web/wasm/poker.wasm.
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
  # Native WebAssembly exception handling so C++ errors (bad card text, etc.)
  # can be caught in the bindings and returned to JS as {ok:false, error}.
  target_compile_options(poker PUBLIC -fwasm-exceptions)
  add_executable(poker_wasm web/bindings/poker_wasm.cpp)
  target_link_libraries(poker_wasm PRIVATE poker)
  set_target_properties(poker_wasm PROPERTIES
    OUTPUT_NAME poker
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/web/wasm)
  target_link_options(poker_wasm PRIVATE
    -lembind
    -fwasm-exceptions
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createPokerModule
    -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1
    -sFILESYSTEM=0)
  return()
endif()

find_package(Threads REQUIRED)
target_link_libraries(poker PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Command-line tool: ./build/equity
# ---------------------------------------------------------------------------
if(POKER_BUILD_CLI)
  add_executable(equity cli/main.cpp)
  target_link_libraries(equity PRIVATE poker)
  target_compile_options(equity PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Benchmarks: ./build/bench
# ---------------------------------------------------------------------------
if(POKER_BUILD_BENCH)
  add_executable(bench bench/bench.cpp)
  target_link_libraries(bench PRIVATE poker)
  target_compile_options(bench PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Tests (Catch2 v3 via FetchContent)
# ---------------------------------------------------------------------------
if(POKER_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(Catch2)

  enable_testing()
  add_executable(poker_tests
    tests/test_card.cpp
    tests/test_evaluator.cpp
    tests/test_reference.cpp
    tests/test_exact.cpp
    tests/test_range.cpp
    tests/test_mc.cpp
  )
  target_link_libraries(poker_tests PRIVATE poker Catch2::Catch2WithMain)
  target_compile_options(poker_tests PRIVATE -Wall -Wextra)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(poker_tests)
endif()
