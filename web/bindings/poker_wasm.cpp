// poker_wasm.cpp - Embind bindings exposing the engine to JavaScript.
//
// Built only by Emscripten (see CMakeLists.txt / scripts/build-wasm.sh). The
// output is an ES module `web/wasm/poker.js` + `poker.wasm`:
//
//   import createPokerModule from './wasm/poker.js';
//   const engine = await createPokerModule();
//   engine.evaluate('As Ks Qs Js Ts');            // {strength, category, description}
//   engine.equity(['AsKs', 'QQ+', 'random'], 'Jc Tc 2d', 200000, 1);
//   engine.equityExact(['AsKs', 'QhQd'], 'Jc Tc 2d');
//
// Every function returns a plain JS object and never throws into JS: errors
// come back as {ok: false, error: "message"} so the UI can show them inline.
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "poker/poker.hpp"

using emscripten::val;

namespace {

val error_result(const std::string& msg) {
  val out = val::object();
  out.set("ok", false);
  out.set("error", msg);
  return out;
}

const char* kind_name(poker::PlayerSpec::Kind k) {
  switch (k) {
    case poker::PlayerSpec::Kind::Hand: return "hand";
    case poker::PlayerSpec::Kind::Range: return "range";
    case poker::PlayerSpec::Kind::Random: return "random";
  }
  return "unknown";
}

val to_js(const poker::EquityResult& r, const std::vector<poker::PlayerSpec>& specs, poker::CardSet board) {
  poker::CardSet fixed = board;
  for (const auto& s : specs) {
    if (s.kind == poker::PlayerSpec::Kind::Hand) fixed |= s.hand;
  }
  val players = val::array();
  for (std::size_t i = 0; i < r.players.size(); ++i) {
    const auto& p = r.players[i];
    const auto& s = specs[i];
    val o = val::object();
    o.set("win", p.win);
    o.set("tie", p.tie);
    o.set("equity", p.equity);
    o.set("stdError", p.std_error);
    o.set("kind", std::string(kind_name(s.kind)));
    o.set("label", s.label);
    double combos = 1;
    if (s.kind == poker::PlayerSpec::Kind::Range) {
      combos = static_cast<double>(poker::remove_conflicts(s.combos, fixed).size());
    } else if (s.kind == poker::PlayerSpec::Kind::Random) {
      combos = static_cast<double>(poker::remove_conflicts(poker::all_combos(), fixed).size());
    }
    o.set("combos", combos);
    if (s.kind == poker::PlayerSpec::Kind::Hand && board.size() >= 3) {
      o.set("madeHand", poker::describe(poker::evaluate(s.hand | board)));
    }
    players.call<void>("push", o);
  }
  val out = val::object();
  out.set("ok", true);
  out.set("exact", r.exact);
  out.set("trials", static_cast<double>(r.trials));
  out.set("rejected", static_cast<double>(r.rejected));
  out.set("players", players);
  return out;
}

std::vector<poker::PlayerSpec> parse_players(const val& players) {
  const auto texts = emscripten::vecFromJSArray<std::string>(players);
  std::vector<poker::PlayerSpec> specs;
  specs.reserve(texts.size());
  for (std::size_t i = 0; i < texts.size(); ++i) {
    try {
      specs.push_back(poker::PlayerSpec::parse(texts[i]));
    } catch (const std::exception& e) {
      throw std::invalid_argument("player " + std::to_string(i + 1) + ": " + e.what());
    }
  }
  return specs;
}

val evaluate_js(const std::string& cards) {
  try {
    const poker::CardSet cs = poker::CardSet::parse(cards);
    if (cs.size() < 5 || cs.size() > 7) return error_result("need 5 to 7 cards");
    const std::uint32_t v = poker::evaluate(cs);
    val out = val::object();
    out.set("ok", true);
    out.set("strength", static_cast<double>(v));
    out.set("category", std::string(poker::category_name(poker::category_of(v))));
    out.set("description", poker::describe(v));
    return out;
  } catch (const std::exception& e) {
    return error_result(e.what());
  }
}

val equity_js(const val& players, const std::string& board_text, double samples, double seed) {
  try {
    const auto specs = parse_players(players);
    const poker::CardSet board = poker::CardSet::parse(board_text);
    if (!(samples >= 1) || samples > 1e10) return error_result("samples must be between 1 and 1e10");
    const auto r = poker::equity_monte_carlo(specs, board, {}, static_cast<std::uint64_t>(samples),
                                             static_cast<std::uint64_t>(std::fabs(seed)), 1);
    return to_js(r, specs, board);
  } catch (const std::exception& e) {
    return error_result(e.what());
  }
}

val equity_exact_js(const val& players, const std::string& board_text) {
  try {
    const auto specs = parse_players(players);
    const poker::CardSet board = poker::CardSet::parse(board_text);
    std::vector<poker::CardSet> hands;
    for (const auto& s : specs) {
      if (s.kind != poker::PlayerSpec::Kind::Hand) {
        return error_result("exact enumeration needs every player to have known cards");
      }
      hands.push_back(s.hand);
    }
    const auto r = poker::equity_exact(hands, board);
    return to_js(r, specs, board);
  } catch (const std::exception& e) {
    return error_result(e.what());
  }
}

val parse_range_js(const std::string& text) {
  try {
    const auto combos = poker::parse_range(text);
    val out = val::object();
    out.set("ok", true);
    out.set("combos", static_cast<double>(combos.size()));
    return out;
  } catch (const std::exception& e) {
    return error_result(e.what());
  }
}

}  // namespace

EMSCRIPTEN_BINDINGS(poker_engine) {
  emscripten::function("evaluate", &evaluate_js);
  emscripten::function("equity", &equity_js);
  emscripten::function("equityExact", &equity_exact_js);
  emscripten::function("parseRange", &parse_range_js);
}
