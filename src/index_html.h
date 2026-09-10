#pragma once
#include <Arduino.h>

// Single self-contained page served from flash. No external assets - the ESP32
// is the only host, and it has no internet access to fetch anything from.
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>FLOATY Flap Console</title>
<style>
  :root {
    color-scheme: light dark;
    --accent: #2f6fed;
    --bg: #f4f5f7;
    --card: #ffffff;
    --line: #d9dce1;
    --text: #14161a;
    --muted: #666c76;
    --ok: #1a7f45;
    --off: #a8332a;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --accent: #6f9dff;
      --bg: #14161a;
      --card: #1e2127;
      --line: #333842;
      --text: #e9ecf1;
      --muted: #9aa2ae;
      --ok: #4cc98a;
      --off: #ff7a6e;
    }
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    padding: 1rem;
    padding-bottom: max(1rem, env(safe-area-inset-bottom));
    background: var(--bg);
    color: var(--text);
    accent-color: var(--accent);
    font: 16px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif;
    max-width: 40rem;
    margin-inline: auto;
  }
  header {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 1rem;
    margin-bottom: 1rem;
  }
  h1 { font-size: 1.125rem; margin: 0; letter-spacing: -0.01em; }
  #link {
    font-size: .8125rem;
    color: var(--muted);
    display: flex;
    align-items: center;
    gap: .4rem;
  }
  #dot {
    width: .55rem; height: .55rem; border-radius: 50%;
    background: var(--off);
    transition: background .2s;
  }
  #dot.up { background: var(--ok); }

  .card {
    background: var(--card);
    border: 1px solid var(--line);
    border-radius: .75rem;
    padding: 1rem;
    margin-bottom: .75rem;
  }
  .row {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    margin-bottom: .5rem;
  }
  .name { font-weight: 600; }
  .pin { font-size: .8125rem; color: var(--muted); font-weight: 400; }
  .row output {
    font-variant-numeric: tabular-nums;
    font-size: 1.5rem;
    font-weight: 600;
    letter-spacing: -0.02em;
  }
  .row output span { font-size: .875rem; color: var(--muted); font-weight: 400; }

  input[type=range] {
    width: 100%;
    height: 2rem;
    margin: 0;
  }
  .ticks {
    display: flex;
    justify-content: space-between;
    font-size: .75rem;
    color: var(--muted);
    font-variant-numeric: tabular-nums;
  }

  /* Sweep range block inside each servo card */
  .range {
    margin-top: .875rem;
    padding-top: .75rem;
    border-top: 1px solid var(--line);
  }
  .rangehead {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: .75rem;
    margin-bottom: .25rem;
  }
  .rangehead .dir {
    font-size: .8125rem;
    color: var(--muted);
    font-variant-numeric: tabular-nums;
  }
  .rrow {
    display: grid;
    grid-template-columns: 2.75rem 1fr 3rem;
    align-items: center;
    gap: .625rem;
  }
  .rrow label { font-size: .8125rem; color: var(--muted); }
  .rrow output {
    text-align: right;
    font-size: .9375rem;
    font-weight: 600;
    font-variant-numeric: tabular-nums;
  }

  .btns { display: flex; flex-wrap: wrap; gap: .5rem; }
  button {
    flex: 1 1 auto;
    min-width: 5.5rem;
    min-height: 2.75rem;
    padding: .5rem .75rem;
    font: inherit;
    font-size: .9375rem;
    color: var(--text);
    background: var(--card);
    border: 1px solid var(--line);
    border-radius: .5rem;
    cursor: pointer;
    touch-action: manipulation;
  }
  button.small {
    flex: none;
    min-width: 0;
    min-height: 2.25rem;
    padding: .25rem .75rem;
    font-size: .8125rem;
  }
  button:hover { border-color: var(--accent); }
  button[aria-pressed=true] {
    background: var(--accent);
    border-color: var(--accent);
    color: #fff;
  }
  button:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
  h2 {
    font-size: .75rem;
    text-transform: uppercase;
    letter-spacing: .06em;
    color: var(--muted);
    margin: 0 0 .625rem;
  }
  .speed { display: flex; align-items: center; gap: .75rem; margin-top: .875rem; }
  .speed label { font-size: .8125rem; color: var(--muted); white-space: nowrap; }
  .speed input { flex: 1; }
  .speed output {
    font-size: .8125rem;
    color: var(--muted);
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
    min-width: 8.5rem;
    text-align: right;
  }
  .hint { font-size: .8125rem; color: var(--muted); margin: .625rem 0 0; }
  @media (max-width: 26rem) {
    .speed { flex-wrap: wrap; }
    .speed output { min-width: 0; }
  }
  @media (prefers-reduced-motion: reduce) {
    * { transition: none !important; }
  }
</style>
</head>
<body>

<header>
  <h1>FLOATY Flap Console</h1>
  <div id="link"><span id="dot"></span><span id="linktext">connecting</span></div>
</header>

<div class="card">
  <div class="row">
    <span class="name">Servo 1 <span class="pin" id="pin1"></span></span>
    <output id="out1" for="s1">90<span>&deg;</span></output>
  </div>
  <input type="range" id="s1" min="0" max="180" value="90" step="1" aria-label="Servo 1 angle">
  <div class="ticks"><span>0</span><span>90</span><span>180</span></div>

  <div class="range">
    <div class="rangehead">
      <h2 style="margin:0">Sweep range <span class="dir" id="dir1"></span></h2>
      <button class="small" data-reverse="1" aria-label="Reverse servo 1 direction">&#8646; Reverse</button>
    </div>
    <div class="rrow">
      <label for="f1">From</label>
      <input type="range" id="f1" min="0" max="180" value="0" step="1">
      <output id="f1v" for="f1">0&deg;</output>
    </div>
    <div class="rrow">
      <label for="t1">To</label>
      <input type="range" id="t1" min="0" max="180" value="180" step="1">
      <output id="t1v" for="t1">180&deg;</output>
    </div>
  </div>
</div>

<div class="card">
  <div class="row">
    <span class="name">Servo 2 <span class="pin" id="pin2"></span></span>
    <output id="out2" for="s2">90<span>&deg;</span></output>
  </div>
  <input type="range" id="s2" min="0" max="180" value="90" step="1" aria-label="Servo 2 angle">
  <div class="ticks"><span>0</span><span>90</span><span>180</span></div>

  <div class="range">
    <div class="rangehead">
      <h2 style="margin:0">Sweep range <span class="dir" id="dir2"></span></h2>
      <button class="small" data-reverse="2" aria-label="Reverse servo 2 direction">&#8646; Reverse</button>
    </div>
    <div class="rrow">
      <label for="f2">From</label>
      <input type="range" id="f2" min="0" max="180" value="180" step="1">
      <output id="f2v" for="f2">180&deg;</output>
    </div>
    <div class="rrow">
      <label for="t2">To</label>
      <input type="range" id="t2" min="0" max="180" value="0" step="1">
      <output id="t2v" for="t2">0&deg;</output>
    </div>
  </div>
</div>

<div class="card">
  <h2>Mode</h2>
  <div class="btns">
    <button data-mode="sweep" aria-pressed="false">Sweep</button>
    <button data-mode="endpoints" aria-pressed="false">Endpoints</button>
    <button data-mode="idle" aria-pressed="false">Hold</button>
  </div>
  <div class="speed">
    <label for="spd">Stroke time</label>
    <input type="range" id="spd" min="150" max="3000" step="10" value="1000">
    <output id="spdval" for="spd">1000 ms</output>
  </div>
  <p class="hint">Both servos share one timing, so they reach their ends together.
    While holding, dragging From or To moves that servo there so you can set
    each end against the linkage.</p>
</div>

<div class="card">
  <h2>Both servos</h2>
  <div class="btns">
    <button data-both="0">0&deg;</button>
    <button data-both="90">Center</button>
    <button data-both="180">180&deg;</button>
  </div>
</div>

<div class="card">
  <h2>Power</h2>
  <div class="btns">
    <button id="attach">Attach</button>
    <button id="detach">Detach (limp)</button>
  </div>
</div>

<script>
const $ = (id) => document.getElementById(id);
const deg = (v) => v + '°';

// The ESP32 serves one request at a time. Queue by key so a fast drag sends
// only its latest value, while a button tap during the drag is still sent
// rather than overwritten.
const pending = new Map();
let inflight = false;

function send(key, path) {
  pending.set(key, path);
  pump();
}

async function pump() {
  if (inflight || pending.size === 0) return;
  const [key, path] = pending.entries().next().value;
  pending.delete(key);
  inflight = true;
  try {
    const r = await fetch(path);
    if (r.ok) applyState(await r.json());
    setLink(r.ok);
  } catch {
    setLink(false);
  } finally {
    inflight = false;
    pump();
  }
}

function setLink(up) {
  $('dot').classList.toggle('up', up);
  $('linktext').textContent = up ? 'connected' : 'no link';
}

// A slider the user is touching, or touched in the last moment, is theirs:
// a poll landing mid-drag must not yank the thumb back to a stale value.
const touched = new WeakMap();
let drags = 0;
const ranges = document.querySelectorAll('input[type=range]');
for (const el of ranges) {
  el.addEventListener('pointerdown', () => { el.dataset.drag = '1'; drags++; });
  const release = () => {
    if (el.dataset.drag === '1') { el.dataset.drag = ''; drags--; }
  };
  el.addEventListener('pointerup', release);
  el.addEventListener('pointercancel', release);
  el.addEventListener('input', () => touched.set(el, performance.now()));
}
const busy = (el) =>
  el.dataset.drag === '1' || performance.now() - (touched.get(el) || 0) < 800;
const setIdle = (el, v) => { if (!busy(el)) el.value = v; };

function showDir(i) {
  const f = +$('f' + i).value, t = +$('t' + i).value;
  $('f' + i + 'v').textContent = deg(f);
  $('t' + i + 'v').textContent = deg(t);
  $('dir' + i).textContent = f === t ? '· no travel'
    : '· ' + Math.abs(t - f) + '° ' + (t > f ? '↑' : '↓');
}

function showStroke(ms) {
  $('spdval').textContent = ms + ' ms · ' + (1000 / (2 * ms)).toFixed(2) + ' flaps/s';
}

function applyState(st) {
  if (!st) return;
  $('out1').innerHTML = st.a1 + '<span>&deg;</span>';
  $('out2').innerHTML = st.a2 + '<span>&deg;</span>';
  setIdle($('s1'), st.a1);
  setIdle($('s2'), st.a2);
  $('pin1').textContent = 'GPIO' + st.p1;
  $('pin2').textContent = 'GPIO' + st.p2;

  setIdle($('f1'), st.r1f); setIdle($('t1'), st.r1t);
  setIdle($('f2'), st.r2f); setIdle($('t2'), st.r2t);
  showDir(1); showDir(2);

  const spd = $('spd');
  spd.min = st.smin; spd.max = st.smax;
  setIdle(spd, st.stroke);
  if (!busy(spd)) showStroke(st.stroke);

  for (const b of document.querySelectorAll('[data-mode]')) {
    b.setAttribute('aria-pressed', String(b.dataset.mode === st.mode));
  }
  $('attach').setAttribute('aria-pressed', String(st.attached));
  $('detach').setAttribute('aria-pressed', String(!st.attached));
}

for (const i of [1, 2]) {
  const pos = $('s' + i);
  pos.addEventListener('input', () => {
    $('out' + i).innerHTML = pos.value + '<span>&deg;</span>';
    send('servo' + i, '/api/servo?i=' + i + '&a=' + pos.value);
  });

  const f = $('f' + i), t = $('t' + i);
  const sendRange = (preview) => {
    showDir(i);
    send('range' + i, '/api/range?i=' + i + '&from=' + f.value +
         '&to=' + t.value + (preview ? '&preview=' + preview : ''));
  };
  f.addEventListener('input', () => sendRange('from'));
  t.addEventListener('input', () => sendRange('to'));

  document.querySelector('[data-reverse="' + i + '"]').addEventListener('click', () => {
    [f.value, t.value] = [t.value, f.value];
    touched.set(f, performance.now());
    touched.set(t, performance.now());
    sendRange(null);
  });
}

const spd = $('spd');
spd.addEventListener('input', () => {
  showStroke(spd.value);
  send('speed', '/api/speed?ms=' + spd.value);
});

for (const b of document.querySelectorAll('[data-both]')) {
  b.addEventListener('click', () => send('both', '/api/both?a=' + b.dataset.both));
}
for (const b of document.querySelectorAll('[data-mode]')) {
  b.addEventListener('click', () => send('mode', '/api/mode?m=' + b.dataset.mode));
}
$('attach').addEventListener('click', () => send('attach', '/api/attach?on=1'));
$('detach').addEventListener('click', () => send('attach', '/api/attach?on=0'));

// Poll so the page tracks sweep mode and commands typed at the serial console.
setInterval(() => {
  if (drags === 0 && pending.size === 0 && document.visibilityState === 'visible') {
    send('state', '/api/state');
  }
}, 400);
send('state', '/api/state');
</script>
</body>
</html>
)rawliteral";
