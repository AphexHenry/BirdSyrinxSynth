"""WP-R5 §4.3: held keys in melodies — renders of the options for the human to choose from.

The three MELODY2 tunes played as a person plays them, with "follow the keyboard":
  held    every note held 0.35 s (detached)
  legato  every note held until the next one (10 ms gap); the last note 0.84 s
Options (core whistle path, cuicui_core.sing, same seed for every option):
  d  today's behaviour (Snap to note 0, Birds 4, the closing call after presses of 0.6 s or more)
  c  the preset "Finch, melody": Snap to note 100 %, Birds 1 — the human's choice (2026-10-13, D-033)
On 2026-10-13 the page also had prototype options, rendered with switches since removed from ModeAB:
  a  the phrase only after a hold threshold (0.4 s, 0.5 s; `phrase_hold_s`), below it one syllable per key
  b  no closing call when the next key follows within 150 ms (`legato_window_s`), and a + b
Their numbers are in agent documents/reports/WP-R5_report.md §6.
Writes listening/R5/held/*.wav, held_metrics.json and index.html (a compact page to compare the options: one row
per tune and playing style, one button per option, switching at the same point while playing).
usage: python/.venv/bin/python scripts/studies/r5_heldkeys.py <bank.json>
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import cuicui_core  # noqa: E402
from melody2_make import MELODIES, VELOCITY, reference  # noqa: E402

OUT = ROOT / "listening/R5/held"
SR = 48000
SEED = 7
BASE = {"perf.mapping": "follow"}
OPTIONS = {
    "d_today": ("today (nothing changed)", {}),
    "c_melody_preset": ("(c) preset idea: Snap to note 100 %, Birds 1", {"perf.snap": 1.0, "perf.polyphony": 1}),
}
STYLES = {"held": "notes held 0.35 s", "legato": "legato (each note held to the next)"}


def notes(m: dict, style: str) -> list[tuple[int, float, float]]:
    out = []
    times = m["times"]
    for k, (n, t) in enumerate(zip(m["notes"], times, strict=True)):
        if style == "held":
            d = 0.35
        else:
            d = (times[k + 1] - t - 0.01) if k + 1 < len(times) else 0.84
        out.append((n, t, d))
    return out


def events(ns: list[tuple[int, float, float]]) -> list[tuple]:
    ev = []
    for n, t, d in ns:
        ev += [(t, "on", n, VELOCITY), (t + d, "off", n, 0.0)]
    return ev


def metrics(log: list[dict], ns: list, bank: dict) -> dict:
    syl = [s for p in bank["phrases"] for s in p["syllables"]]
    rule = [s["anchors"]["rule"] for s in syl]
    on = [abs(rule[e["syllable"]] + e["shift_cents"] - e["target_cents"]) <= 50.0 for e in log]
    return {"keys": len(ns), "syllables": len(log), "syllables_per_key": round(len(log) / len(ns), 2),
            "on_the_note_pct": round(100.0 * float(np.mean(on)), 0) if on else 0.0,
            "release_calls": sum(1 for e in log if e["release_alarm"])}


def main() -> None:
    bank_path = Path(sys.argv[1])
    bank = json.loads(bank_path.read_text())
    OUT.mkdir(parents=True, exist_ok=True)
    out = {}
    for mid, m in MELODIES.items():
        for style in STYLES:
            ns = notes(m, style)
            dur = ns[-1][1] + ns[-1][2] + 1.6
            renders = {}
            for oid, (_, opt) in OPTIONS.items():
                audio, log = cuicui_core.sing(str(bank_path), events(ns), {**BASE, **opt}, float(SR), dur, SEED, 256)
                renders[oid] = (np.asarray(audio, dtype=np.float64), log)
                out[f"{mid}_{style}_{oid}"] = metrics(log, ns, bank)
            g = 10 ** (-3 / 20) / max(np.max(np.abs(y)) for y, _ in renders.values())  # one gain per group
            for oid, (y, _) in renders.items():
                sf.write(OUT / f"{mid}_{style}_{oid}.wav", (y * g).astype(np.float32), SR, subtype="FLOAT")
            n = len(next(iter(renders.values()))[0])
            ref = reference(m, n)
            bird = np.sqrt(np.mean(next(iter(renders.values()))[0][:, 0] ** 2)) * g
            ref *= 0.7 * bird / (np.sqrt(np.mean(ref**2)) + 1e-12)
            sf.write(OUT / f"{mid}_{style}_reference.wav", np.stack([ref, ref], 1).astype(np.float32), SR,
                     subtype="FLOAT")
    (OUT / "held_metrics.json").write_text(json.dumps({"bank": str(bank_path), "hash": bank["hash"], "seed": SEED,
                                                       "metrics": out}, indent=1))
    for k, v in out.items():
        print(f"{k:42s} {v}")
    write_page(out, bank["hash"])


TEXT = {
    "held": "Today the bird adds its own melody between your notes (about 3 syllables per key, most off the note). "
            "(a) gives one syllable per key, like taps. (c) keeps the extra syllables but puts them all on your note. "
            "(b) changes nothing here: no note is held long enough for the closing call.",
    "legato": "Today: extra syllables on every note, and the long rising call after the long notes. "
              "(a) 0.5 s keeps the extra syllables for the long notes only. (b) keeps the call for the end of the line. "
              "(c) puts every syllable on the note.",
}


def write_page(metrics_: dict, bank_hash: str) -> None:
    """A compact comparison page: rows = tune x playing style, buttons = options (+ the tune on a soft tone)."""
    rows = []
    for mid, m in MELODIES.items():
        for style, stitle in STYLES.items():
            cells = [f'<button data-src="{mid}_{style}_reference.wav" class="ref">♪ the tune</button>']
            for oid, (title, _) in OPTIONS.items():
                mm = metrics_[f"{mid}_{style}_{oid}"]
                cells.append(
                    f'<div class="opt"><button data-src="{mid}_{style}_{oid}.wav">▶ {title}</button>'
                    f'<span class="m">{mm["syllables_per_key"]:.1f} per key · {mm["on_the_note_pct"]:.0f} % on the note'
                    f' · {mm["release_calls"]} call{"s" if mm["release_calls"] != 1 else ""}</span></div>')
            rows.append(f'<section class="card"><h2>{m["title"]} — {stitle}</h2><p class="hint">{TEXT[style]}</p>'
                        f'<div class="row">{"".join(cells)}<button class="stop">■ stop</button></div></section>')
    html = PAGE.replace("<!--ROWS-->", "\n".join(rows)).replace("<!--BANK-->", bank_hash)
    (OUT / "index.html").write_text(html)


PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Held Keys Choice</title>
<style>
  :root { --bg: #f7f6f1; --card: #ffffff; --fg: #1f1e1b; --muted: #6d6b64; --line: #e2e0d8; --accent: #2e6b4f; --chip: #eef3ef; }
  @media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
    --bg: #141413; --card: #1d1d1b; --fg: #ecebe5; --muted: #9b998f; --line: #34332f; --accent: #7cc49d; --chip: #23302a; } }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--fg); font: 15px/1.45 -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif; }
  main { max-width: 1100px; margin: 0 auto; padding: 24px 16px 64px; }
  h1 { font-size: 1.45rem; margin: 0 0 6px; } h2 { font-size: 1.05rem; margin: 0 0 4px; }
  .card { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 14px 16px; margin-bottom: 12px; }
  .hint { color: var(--muted); font-size: .9rem; margin: 0 0 8px; }
  .row { display: flex; flex-wrap: wrap; gap: 8px; align-items: flex-start; }
  .opt { display: flex; flex-direction: column; gap: 2px; max-width: 220px; }
  .m { color: var(--muted); font-size: .78rem; }
  button { font: inherit; border-radius: 8px; border: 1px solid var(--line); background: var(--card); color: var(--fg); padding: 6px 10px; cursor: pointer; text-align: left; }
  button.ref { background: var(--chip); }
  button.playing { outline: 3px solid var(--accent); outline-offset: 1px; }
  ul { padding-left: 1.2rem; } li { margin: 3px 0; }
</style>
</head>
<body>
<main>
  <h1>Held keys in tunes — which behaviour do you want?</h1>
  <div class="card">
    <p>Each row is one tune played one way; each button is one option. Click another button while it plays to switch
    at the same point. The numbers under each button: syllables per key, how many sit on your note, and how many
    long rising calls (the "alarm", D-030) were sung. Same bird (bank <!--BANK-->), same random draws in every option.</p>
    <ul>
      <li><b>today</b>: nothing changed (Snap to note 0, Birds 4, the call after any press of 0.6 s or more).</li>
      <li><b>(a)</b>: the bird goes on with its phrase only if you hold the key longer than 0.4 s or 0.5 s; below that, one syllable per key.</li>
      <li><b>(b)</b>: no closing call when the next key follows within 150 ms (legato): the call ends a line, not every long note.</li>
      <li><b>(a)+(b)</b>: both (0.5 s and 150 ms).</li>
      <li><b>(c)</b>: a factory preset idea, "Finch, melody": follow the keyboard, Snap to note 100 %, Birds 1.</li>
    </ul>
    <p class="hint">Reply with what you want, possibly several, e.g. "a 0.5 + b", "c", or "today".</p>
  </div>
<!--ROWS-->
</main>
<script>
(function () {
  "use strict";
  let cur = null; // {audio, button}
  const cache = {};
  function audioFor(src) { if (!cache[src]) { const a = new Audio(src); a.preload = "auto"; cache[src] = a; } return cache[src]; }
  document.querySelectorAll("button[data-src]").forEach((b) => audioFor(b.dataset.src));
  function stop() { if (cur) { cur.audio.pause(); cur.button.classList.remove("playing"); cur = null; } }
  document.querySelectorAll("button[data-src]").forEach((b) => b.addEventListener("click", () => {
    const a = audioFor(b.dataset.src);
    const sameRow = cur && cur.button.closest(".row") === b.closest(".row");
    const t = sameRow && !cur.audio.ended ? cur.audio.currentTime : 0;
    stop();
    try { a.currentTime = t; } catch (e) { /* not seekable yet: start at 0 */ }
    a.play();
    b.classList.add("playing");
    cur = { audio: a, button: b };
    a.onended = () => { if (cur && cur.audio === a) stop(); };
  }));
  document.querySelectorAll("button.stop").forEach((b) => b.addEventListener("click", stop));
  document.addEventListener("keydown", (e) => { if (e.key === "Escape" || e.key === " ") { e.preventDefault(); stop(); } });
})();
</script>
</body>
</html>
"""


if __name__ == "__main__":
    main()
