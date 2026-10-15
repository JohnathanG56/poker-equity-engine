// worker.js - runs the WebAssembly engine off the main thread.
//
// Protocol
//   main -> worker  {type: 'compute', id, players: string[], board: string,
//                    exact: boolean, maxSamples: number, seed: number}
//   worker -> main  {type: 'ready'}
//                   {type: 'progress', id, result, done, elapsedMs}
//                   {type: 'error', id, message}
//
// Monte Carlo runs in growing batches and posts after each one, so the UI can
// animate the bars while the estimate converges. Between batches we yield to
// the event loop; if a newer 'compute' message has arrived, the old loop
// notices that its id is stale and stops. That is how typing in a range box
// never queues up stale work.
import createPokerModule from './wasm/poker.js';

const enginePromise = createPokerModule();
let currentId = 0;

enginePromise.then(
  () => self.postMessage({ type: 'ready' }),
  (err) => self.postMessage({ type: 'fatal', message: String(err) }),
);

const yieldToEventLoop = () => new Promise((resolve) => setTimeout(resolve, 0));

/** Combine per-batch results into one running estimate. */
function makeAccumulator() {
  let n = 0;
  let rejected = 0;
  let players = null;
  return {
    add(r) {
      const b = r.trials;
      if (!players) {
        players = r.players.map((p) => ({ ...p, sEq: 0, sWin: 0, sTie: 0, sVar: 0 }));
      }
      r.players.forEach((p, i) => {
        const a = players[i];
        a.sEq += p.equity * b;
        a.sWin += p.win * b;
        a.sTie += p.tie * b;
        // Var(mean of batch) = se^2  ->  contributes (b * se)^2 to Var(sum)
        a.sVar += (b * p.stdError) ** 2;
        a.combos = p.combos;
      });
      n += b;
      rejected += r.rejected;
    },
    snapshot() {
      return {
        ok: true,
        exact: false,
        trials: n,
        rejected,
        players: players.map((a) => ({
          label: a.label,
          kind: a.kind,
          combos: a.combos,
          madeHand: a.madeHand,
          equity: a.sEq / n,
          win: a.sWin / n,
          tie: a.sTie / n,
          stdError: Math.sqrt(a.sVar) / n,
        })),
      };
    },
  };
}

self.onmessage = async (event) => {
  const msg = event.data;
  if (msg.type !== 'compute') return;
  currentId = msg.id;
  const engine = await enginePromise;
  if (msg.id !== currentId) return;

  const t0 = performance.now();
  if (msg.exact) {
    const r = engine.equityExact(msg.players, msg.board);
    if (!r.ok) {
      self.postMessage({ type: 'error', id: msg.id, message: r.error });
      return;
    }
    self.postMessage({ type: 'progress', id: msg.id, result: r, done: true, elapsedMs: performance.now() - t0 });
    return;
  }

  const acc = makeAccumulator();
  let batch = 20000;
  let done = 0;
  let seed = msg.seed;
  while (done < msg.maxSamples) {
    if (msg.id !== currentId) return; // superseded by a newer request
    const size = Math.min(batch, msg.maxSamples - done);
    const r = engine.equity(msg.players, msg.board, size, seed++);
    if (!r.ok) {
      self.postMessage({ type: 'error', id: msg.id, message: r.error });
      return;
    }
    acc.add(r);
    done += size;
    self.postMessage({
      type: 'progress',
      id: msg.id,
      result: acc.snapshot(),
      done: done >= msg.maxSamples,
      elapsedMs: performance.now() - t0,
    });
    batch = Math.min(batch * 2, 400000);
    await yieldToEventLoop();
  }
};
