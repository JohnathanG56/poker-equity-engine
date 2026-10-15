// Node smoke test for the WebAssembly build.
//
//   node web/test/smoke.mjs                 # wasm-only checks
//   node web/test/smoke.mjs build/equity    # + bit-for-bit comparison with the native CLI
//
// Exits non-zero on the first failure.
import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const modulePath = path.join(here, '..', 'wasm', 'poker.js');
if (!existsSync(modulePath)) {
  console.error(`missing ${modulePath} - run scripts/build-wasm.sh first`);
  process.exit(1);
}
const { default: createPokerModule } = await import(modulePath);
const engine = await createPokerModule();

let passed = 0;
function check(cond, msg) {
  if (!cond) {
    console.error(`FAIL: ${msg}`);
    process.exit(1);
  }
  passed++;
}
const sum = (r) => r.players.reduce((s, p) => s + p.equity, 0);

// --- evaluate -------------------------------------------------------------
const royal = engine.evaluate('As Ks Qs Js Ts 2d 3c');
check(royal.ok && royal.description === 'Royal Flush', 'evaluate royal flush');
const wheel = engine.evaluate('Ac 2d 3h 4s 5d Kc 9h');
check(wheel.ok && wheel.category === 'Straight', 'evaluate wheel is a straight');
check(royal.strength > wheel.strength, 'royal flush beats a straight');
check(!engine.evaluate('As Ks').ok, 'evaluate rejects 2 cards');
check(!engine.evaluate('As As Ks Qs Js').ok, 'evaluate rejects duplicates');

// --- exact ------------------------------------------------------------------
const flop = engine.equityExact(['AsKs', 'QhQd'], 'Jc Tc 2d');
check(flop.ok && flop.exact, 'exact on the flop succeeds');
check(flop.trials === 990, `flop enumerates C(45,2)=990 boards (got ${flop.trials})`);
check(Math.abs(sum(flop) - 1) < 1e-12, 'exact equities sum to 1');
const sym = engine.equityExact(['AhKd', 'AdKh'], '');
check(Math.abs(sym.players[0].equity - 0.5) < 1e-12, 'suit-symmetric hands split exactly');
check(!engine.equityExact(['AsKs', 'QQ'], '').ok, 'exact refuses ranges');

// --- Monte Carlo -------------------------------------------------------------
const mc = engine.equity(['AsKs', 'QhQd'], 'Jc Tc 2d', 400000, 3);
check(mc.ok && !mc.exact && mc.trials === 400000, 'MC runs the requested samples');
for (let i = 0; i < 2; i++) {
  const err = Math.abs(mc.players[i].equity - flop.players[i].equity);
  check(err <= 4 * mc.players[i].stdError, `MC within 4 SE of exact (player ${i + 1}, err ${err})`);
}
const again = engine.equity(['AsKs', 'QhQd'], 'Jc Tc 2d', 400000, 3);
check(again.players[0].equity === mc.players[0].equity, 'MC is deterministic for a seed');

const ranges = engine.equity(['AhAd', 'QQ+, AKs, ATo+, 76s, KQ', 'random'], '', 200000, 1);
check(ranges.ok && Math.abs(sum(ranges) - 1) < 1e-9, 'ranges + random: equities sum to 1');
check(ranges.players[1].combos > 0 && ranges.players[2].combos === 1225, 'combo counts after card removal');

const bad = engine.equity(['AsKs', 'AsQd'], '', 1000, 1);
check(!bad.ok && /conflict/.test(bad.error), 'card conflicts are reported, not thrown');
check(!engine.equity(['AsKs', 'ZZ'], '', 1000, 1).ok, 'bad range text is reported');
check(engine.parseRange('22-55').combos === 24, 'parseRange counts combos');

// --- optional: native CLI must agree bit-for-bit ------------------------------
const native = process.argv[2];
if (native) {
  const cases = [
    { players: ['AsKs', 'QhQd'], board: 'Jc Tc 2d' },
    { players: ['AhAd', 'QQ+, AKs', 'random'], board: '' },
    { players: ['8h8d', '22-55, A2s-A5s', 'random', 'random'], board: 'Kd 7c 2c' },
  ];
  for (const c of cases) {
    // Exact (only when all hands are known)
    if (c.players.every((p) => /^([2-9TJQKA][cdhs]){2}$/.test(p))) {
      const n = JSON.parse(execFileSync(native, [...c.players, '--board', c.board, '--exact', '--json']).toString());
      const w = engine.equityExact(c.players, c.board);
      n.players.forEach((p, i) => check(p.equity === w.players[i].equity, `native exact == wasm exact (${c.players})`));
    }
    // Monte Carlo with one thread and the same seed: identical streams, so
    // identical integer tallies and identical doubles.
    const n = JSON.parse(
      execFileSync(native, [...c.players, '--board', c.board, '-n', '200000', '-t', '1', '-s', '11', '--json']).toString(),
    );
    const w = engine.equity(c.players, c.board, 200000, 11);
    check(n.trials === w.trials && n.rejected === w.rejected, `native/wasm MC sample counts agree (${c.players})`);
    n.players.forEach((p, i) => {
      check(p.equity === w.players[i].equity, `native MC == wasm MC bit-for-bit (${c.players}, player ${i + 1})`);
    });
  }
}

console.log(`wasm smoke test: ${passed} checks passed${native ? ' (including native CLI comparison)' : ''}`);
