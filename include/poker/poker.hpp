// poker.hpp - Umbrella header for the Poker Equity Engine public API.
#pragma once

#include "poker/card.hpp"       // Card, CardSet, parsing/printing
#include "poker/equity.hpp"     // equity_exact, equity_monte_carlo, PlayerSpec
#include "poker/evaluator.hpp"  // evaluate7, HandCategory, describe
#include "poker/range.hpp"      // parse_range ("QQ+, AKs, ...")
