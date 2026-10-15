// app.js - UI for the Poker Equity Engine web demo (plain ES modules, no framework).
//
// State lives in one object; every change calls render() and schedules a
// computation. All heavy lifting happens in worker.js so the page never
// freezes, even while Monte Carlo is running.

const RANKS = 'AKQJT98765432';
const SUITS = ['s', 'h', 'd', 'c'];
const SUIT_SYMBOL = { s: '♠', h: '♥', d: '♦', c: '♣' };
const PLAYER_COLORS = ['#3fb68b', '#f0a73a', '#58a6ff', '#e36fbf', '#a371f7', '#f0716b'];
const MAX_PLAYERS = 6;
const MIN_PLAYERS = 2;
const MC_SAMPLES = 3_000_000;

const newPlayer = (mode = 'cards') => ({ mode, cards: [null, null], range: '' });

let state = {
  board: [null, null, null, null, null],
  players: [newPlayer(), newPlayer()],
  active: { type: 'player', p: 0, i: 0 },
};

let lastResult = null; // last successful result (for display while recomputing)
let lastError = null;
let computeId = 0;
let workerReady = false;
let busy = false;

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
const worker = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
worker.onmessage = (e) => {
  const msg = e.data;
  if (msg.type === 'ready') {
    workerReady = true;
    schedule(0);
    return;
  }
  if (msg.type === 'fatal') {
    setStatus(`Could not load the WebAssembly module: ${msg.message}`, 'error');
    return;
  }
  if (msg.id !== computeId) return; // stale
  if (msg.type === 'error') {
    busy = false;
    lastError = msg.message;
    lastResult = null;
    renderResults();
    setStatus(msg.message, 'error');
    return;
  }
  if (msg.type === 'progress') {
    busy = !msg.done;
    lastError = null;
    lastResult = msg.result;
    renderResults();
    const r = msg.result;
    const secs = (msg.elapsedMs / 1000).toFixed(2);
    if (r.exact) {
      setStatus(`Exact · enumerated ${fmtInt(r.trials)} boards in ${secs}s`, 'ok');
    } else {
      const maxSe = Math.max(...r.players.map((p) => p.stdError));
      setStatus(
        `Monte Carlo · ${fmtInt(r.trials)} samples · max ±${(100 * maxSe).toFixed(2)}% (1 SE) · ${secs}s`,
        msg.done ? 'ok' : 'busy',
      );
    }
  }
};
worker.onerror = (e) => {
  setStatus(
    'WebAssembly worker failed to start. Build it first with scripts/build-wasm.sh and serve this folder over HTTP.',
    'error',
  );
  console.error(e);
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
const $ = (sel) => document.querySelector(sel);
const fmtInt = (n) => Math.round(n).toLocaleString('en-US');
const pct = (x, d = 1) => `${(100 * x).toFixed(d)}%`;

function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') node.className = v;
    else if (k.startsWith('on')) node.addEventListener(k.slice(2), v);
    else if (v !== undefined && v !== null && v !== false) node.setAttribute(k, v === true ? '' : v);
  }
  for (const c of children.flat()) {
    if (c === null || c === undefined || c === false) continue;
    node.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return node;
}

function cardFace(card, cls = 'card') {
  const suit = card[1];
  return el(
    'div',
    { class: `${cls} ${suit === 's' ? 's-suit' : suit}`, 'aria-label': card },
    el('span', { class: 'r' }, card[0] === 'T' ? '10' : card[0]),
    el('span', { class: 's' }, SUIT_SYMBOL[suit]),
  );
}

function usedCards() {
  const used = new Set();
  state.board.forEach((c) => c && used.add(c));
  state.players.forEach((p) => p.mode === 'cards' && p.cards.forEach((c) => c && used.add(c)));
  return used;
}

function slotsInOrder() {
  const out = [];
  state.players.forEach((p, pi) => {
    if (p.mode === 'cards') [0, 1].forEach((i) => out.push({ type: 'player', p: pi, i }));
  });
  [0, 1, 2, 3, 4].forEach((i) => out.push({ type: 'board', i }));
  return out;
}

const sameSlot = (a, b) => a && b && a.type === b.type && a.i === b.i && (a.type === 'board' || a.p === b.p);
const getSlot = (s) => (s.type === 'board' ? state.board[s.i] : state.players[s.p].cards[s.i]);
function setSlot(s, card) {
  if (s.type === 'board') state.board[s.i] = card;
  else state.players[s.p].cards[s.i] = card;
}

function advanceActive() {
  const slots = slotsInOrder();
  const start = slots.findIndex((s) => sameSlot(s, state.active));
  for (let k = 1; k <= slots.length; k++) {
    const s = slots[(start + k) % slots.length];
    if (!getSlot(s)) {
      state.active = s;
      return;
    }
  }
  state.active = null;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
function slotButton(slot, label) {
  const card = getSlot(slot);
  const active = sameSlot(slot, state.active);
  return el(
    'button',
    {
      class: `slot${card ? ' filled' : ''}${active ? ' active' : ''}`,
      title: card ? 'Click to remove' : 'Click, then pick a card',
      'aria-label': card ? `${label}: ${card}, click to remove` : `${label}: empty`,
      onclick: () => {
        if (card) setSlot(slot, null);
        state.active = slot;
        changed();
      },
    },
    card ? cardFace(card) : label,
  );
}

function renderBoard() {
  const board = $('#board');
  board.replaceChildren(
    slotButton({ type: 'board', i: 0 }, 'Flop'),
    slotButton({ type: 'board', i: 1 }, 'Flop'),
    slotButton({ type: 'board', i: 2 }, 'Flop'),
    el('div', { class: 'gap' }),
    slotButton({ type: 'board', i: 3 }, 'Turn'),
    el('div', { class: 'gap' }),
    slotButton({ type: 'board', i: 4 }, 'River'),
  );
  const n = state.board.filter(Boolean).length;
  $('#board-hint').textContent =
    n === 1 || n === 2 ? 'A board needs 0, 3, 4 or 5 cards.' : n === 0 ? 'Preflop. Add 3 cards for a flop.' : '';
}

function renderPicker() {
  const used = usedCards();
  const picker = $('#picker');
  const buttons = [];
  for (const s of SUITS) {
    for (const r of RANKS) {
      const card = r + s;
      buttons.push(
        el(
          'button',
          {
            class: `pick ${s === 's' ? 's-suit' : s}`,
            disabled: used.has(card),
            'aria-label': card,
            onclick: () => {
              if (!state.active) state.active = slotsInOrder().find((sl) => !getSlot(sl)) || null;
              if (!state.active) return;
              setSlot(state.active, card);
              advanceActive();
              changed();
            },
          },
          el('span', {}, r === 'T' ? '10' : r),
          el('span', {}, SUIT_SYMBOL[s]),
        ),
      );
    }
  }
  picker.replaceChildren(...buttons);
  const a = state.active;
  $('#picker-target').textContent = a
    ? a.type === 'board'
      ? `Picking: ${['flop', 'flop', 'flop', 'turn', 'river'][a.i]} card`
      : `Picking: player ${a.p + 1}, card ${a.i + 1}`
    : 'Select a slot, then a card';
}

function renderPlayers() {
  const root = $('#players');
  const focused = document.activeElement?.dataset?.player;
  const caret = document.activeElement?.selectionStart;
  root.replaceChildren(
    ...state.players.map((p, pi) => {
      const modeBtn = (mode, label) =>
        el(
          'button',
          {
            class: p.mode === mode ? 'on' : '',
            onclick: () => {
              p.mode = mode;
              if (mode !== 'cards' && state.active?.type === 'player' && state.active.p === pi) state.active = null;
              if (mode === 'cards') state.active = { type: 'player', p: pi, i: p.cards[0] ? 1 : 0 };
              changed();
            },
          },
          label,
        );

      let input;
      if (p.mode === 'cards') {
        input = el(
          'div',
          { class: 'hand' },
          slotButton({ type: 'player', p: pi, i: 0 }, '?'),
          slotButton({ type: 'player', p: pi, i: 1 }, '?'),
        );
      } else if (p.mode === 'range') {
        input = el('input', {
          class: 'range-input',
          type: 'text',
          value: p.range,
          placeholder: 'e.g. QQ+, AKs, ATo+, 76s',
          spellcheck: 'false',
          autocomplete: 'off',
          'data-player': String(pi),
          'aria-label': `Player ${pi + 1} range`,
          oninput: (e) => {
            p.range = e.target.value;
            schedule(250);
          },
        });
      } else {
        input = el(
          'div',
          { class: 'random-chip', title: 'Any two cards' },
          el('div', { class: 'card-back' }, '?'),
          el('div', { class: 'card-back' }, '?'),
        );
      }

      const title = el(
        'div',
        { class: 'player-title' },
        el('span', { class: 'player-color', style: `background:${PLAYER_COLORS[pi]}` }),
        `Player ${pi + 1}`,
        state.players.length > MIN_PLAYERS
          ? el(
              'button',
              {
                class: 'remove',
                title: 'Remove player',
                'aria-label': `Remove player ${pi + 1}`,
                onclick: () => {
                  state.players.splice(pi, 1);
                  state.active = null;
                  changed();
                },
              },
              '×',
            )
          : null,
      );

      return el(
        'div',
        { class: 'panel player', 'data-index': String(pi) },
        el(
          'div',
          { class: 'left' },
          title,
          el('div', { class: 'seg' }, modeBtn('cards', 'Cards'), modeBtn('range', 'Range'), modeBtn('random', 'Random')),
          input,
        ),
        el(
          'div',
          { class: 'result', id: `result-${pi}` },
          el(
            'div',
            { class: 'result-top' },
            el('span', { class: 'equity' }, '–'),
            el('span', { class: 'se' }, ''),
          ),
          el('div', { class: 'bar' }, el('div', { style: `background:${PLAYER_COLORS[pi]}` })),
          el('div', { class: 'meta' }),
        ),
      );
    }),
  );
  if (focused !== undefined) {
    const inp = root.querySelector(`input[data-player="${focused}"]`);
    if (inp) {
      inp.focus();
      if (caret !== undefined && caret !== null) inp.setSelectionRange(caret, caret);
    }
  }
  $('#add-player').disabled = state.players.length >= MAX_PLAYERS;
}

function renderResults() {
  state.players.forEach((p, pi) => {
    const box = document.getElementById(`result-${pi}`);
    if (!box) return;
    const r = lastResult?.players?.[pi];
    const [eqEl, seEl] = box.querySelectorAll('.result-top span');
    const barFill = box.querySelector('.bar > div');
    const meta = box.querySelector('.meta');
    if (!r) {
      eqEl.textContent = '–';
      seEl.textContent = '';
      barFill.style.width = '0%';
      meta.replaceChildren();
      return;
    }
    eqEl.textContent = pct(r.equity, 2);
    seEl.textContent = lastResult.exact ? 'exact' : `± ${(100 * r.stdError).toFixed(2)}%`;
    barFill.style.width = `${(100 * r.equity).toFixed(2)}%`;
    const parts = [el('span', {}, `Win ${pct(r.win)}`), el('span', {}, `Tie ${pct(r.tie)}`)];
    if (r.kind !== 'hand') parts.push(el('span', {}, `${fmtInt(r.combos)} combos`));
    if (r.madeHand) parts.push(el('span', { class: 'made' }, r.madeHand));
    meta.replaceChildren(...parts);
  });
}

function render() {
  renderBoard();
  renderPicker();
  renderPlayers();
  renderResults();
}

function setStatus(text, kind) {
  const s = $('#status');
  s.className = `status${kind === 'error' ? ' error' : ''}`;
  s.replaceChildren(el('span', { class: `dot${kind === 'busy' ? ' busy' : ''}` }), el('span', {}, text));
}

// ---------------------------------------------------------------------------
// Computation scheduling
// ---------------------------------------------------------------------------
let timer = null;
function schedule(delay = 120) {
  clearTimeout(timer);
  timer = setTimeout(compute, delay);
}

function changed() {
  render();
  schedule();
}

function compute() {
  if (!workerReady) return;
  const board = state.board.filter(Boolean);
  if (board.length === 1 || board.length === 2) {
    computeId++; // cancel anything running
    lastResult = null;
    renderResults();
    setStatus('Waiting for a valid board (0, 3, 4 or 5 cards).', 'ok');
    return;
  }
  const players = [];
  for (let i = 0; i < state.players.length; i++) {
    const p = state.players[i];
    if (p.mode === 'random') players.push('random');
    else if (p.mode === 'range') {
      if (!p.range.trim()) {
        computeId++;
        lastResult = null;
        renderResults();
        setStatus(`Player ${i + 1}: type a range such as "QQ+, AKs".`, 'ok');
        return;
      }
      players.push(p.range);
    } else {
      const cards = p.cards.filter(Boolean);
      if (cards.length === 1) {
        computeId++;
        lastResult = null;
        renderResults();
        setStatus(`Player ${i + 1}: pick a second card (or leave both empty for a random hand).`, 'ok');
        return;
      }
      players.push(cards.length === 2 ? cards.join('') : 'random');
    }
  }
  const allKnown = players.every((t) => /^([2-9TJQKA][shdc]){2}$/.test(t));
  computeId++;
  busy = true;
  setStatus(allKnown ? 'Enumerating every board…' : 'Simulating…', 'busy');
  worker.postMessage({
    type: 'compute',
    id: computeId,
    players,
    board: board.join(' '),
    exact: allKnown,
    maxSamples: MC_SAMPLES,
    seed: 1,
  });
}

// ---------------------------------------------------------------------------
// Presets & controls
// ---------------------------------------------------------------------------
function cardsPlayer(a, b) {
  return { mode: 'cards', cards: [a, b], range: '' };
}
function rangePlayer(text) {
  return { mode: 'range', cards: [null, null], range: text };
}
function randomPlayer() {
  return { mode: 'random', cards: [null, null], range: '' };
}

const PRESETS = {
  aksqq: { board: [], players: [cardsPlayer('As', 'Ks'), cardsPlayer('Qh', 'Qd')] },
  flop: { board: ['Jc', 'Tc', '2d'], players: [cardsPlayer('As', 'Ks'), cardsPlayer('Qh', 'Qd')] },
  ranges: { board: [], players: [rangePlayer('QQ+, AKs, AKo'), rangePlayer('22-99, ATs+, KQs, AJo+')] },
  sixway: {
    board: [],
    players: [
      cardsPlayer('Ah', 'Ad'),
      cardsPlayer('Ks', 'Kc'),
      cardsPlayer('Qs', 'Qc'),
      cardsPlayer('Jh', 'Th'),
      cardsPlayer('9c', '9d'),
      cardsPlayer('5s', '4s'),
    ],
  },
  random: { board: [], players: [cardsPlayer('As', 'Ad'), randomPlayer(), randomPlayer(), randomPlayer()] },
};

function loadPreset(name) {
  const p = PRESETS[name];
  state = {
    board: [...p.board, null, null, null, null, null].slice(0, 5),
    players: p.players.map((x) => ({ ...x, cards: [...x.cards] })),
    active: null,
  };
  changed();
}

document.querySelectorAll('[data-preset]').forEach((b) => b.addEventListener('click', () => loadPreset(b.dataset.preset)));
$('#add-player').addEventListener('click', () => {
  if (state.players.length >= MAX_PLAYERS) return;
  state.players.push(newPlayer('random'));
  changed();
});
$('#reset').addEventListener('click', () => {
  state = { board: [null, null, null, null, null], players: [newPlayer(), newPlayer()], active: { type: 'player', p: 0, i: 0 } };
  changed();
});
$('#clear-board').addEventListener('click', () => {
  state.board = [null, null, null, null, null];
  state.active = { type: 'board', i: 0 };
  changed();
});

// Start with a meaningful spot so the page is never empty.
loadPreset('aksqq');
setStatus('Loading WebAssembly…', 'busy');
