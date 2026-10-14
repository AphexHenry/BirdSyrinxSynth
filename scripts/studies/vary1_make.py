"""VARY1 — variations within the bird's style: nudge vs shape model, and new order (WP-R3).

MUSHRA-style page (template vary1_page.html). Each screen: the real recording of the bird singing two or
three songs (hidden reference among the versions too), and versions sung by the whistle voice. The
versions are not the same notes: the question is how much they sound like this bird singing.

Part A — "the same song again" (syllable variation). Versions:
  real     the whistle on the bird's own renditions (its real variation)
  same     the first song's rendition, sung again identically at each song's place (today's repetition)
  nudge1   that rendition, every time varied by nudge at the bird's natural spread (v = 1)
  shape1   … by the shape model, v = 1
  nudge2   nudge, v = 2 (twice the bird's spread)
  shape2   shape model, v = 2
  anchor   the real renditions with strong harmonics (buzzy)
Part B — "new songs" (order). Versions:
  real     the whistle on the bird's own songs
  order2   new songs from the bird's transitions (order 2), real renditions
  order1   new songs, order 1 (freer)
  shuffle  the real songs' syllables in a random order (same gaps; long calls stay in place)
  order2s  order 2 + the shape model, v = 1 (the full proposal)
  anchor   the real songs with strong harmonics
All whistle versions: the recording's background bed added, loudness matched on the bird's syllables.
Pre-registered decision rule: agent documents/studies/VARY1_variations.md.

usage: python/.venv/bin/python scripts/studies/vary1_make.py [--provisional]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(HERE))
import r3_render as R  # noqa: E402

from cuicui.variation import Model  # noqa: E402

OUT = ROOT / "listening/R3/vary1"
SEED = 20261014
BANKS = {"22938": ROOT / "assets/voicebanks/hf_22938/bank.json",
         "22937": ROOT / "assets/voicebanks/hf_22937/bank.json"}
PROVISIONAL = {"22938": ROOT / "build/r3/bank_22938_full.json",
               "22937": ROOT / "build/r3/bank_22937.json"}
# (screen id, part, recording, clip start s, clip end s); the phrases inside the clip are the songs
PASSAGES = [
    ("A1", "A", "22937", 16.15, 27.25),   # song type 1 sung 3 times
    ("A2", "A", "22938", 63.80, 73.17),   # two renditions of one song family (the next starts 73.18)
    ("B1", "B", "22938", 43.10, 51.70),
    ("B2", "B", "22938", 139.40, 149.20),
]
PAD_S = 0.3


def songs_in(model: Model, bank: dict, t0: float, t1: float) -> list[tuple[float, int]]:
    """[(song onset s, phrase position)] of the bank phrases fully inside [t0, t1]."""
    out = []
    for pos, ph in enumerate(model.phrases):
        start = bank["phrases"][pos]["start_s"]
        end = start + (ph[-1].t_ms + len(ph[-1].cents) - ph[0].t_ms) / 1000.0
        if start >= t0 and end <= t1:
            out.append((start, pos))
    return out


def events_of(model: Model, plan: list[tuple[int, int]], onset_s: float, rng, v: float = 0.0,
              mode: str = "shape") -> list[tuple[float, np.ndarray, np.ndarray]]:
    """A phrase plan [(syllable index, onset ms)] → whistle events, each syllable varied by v."""
    ev = []
    for i, t_ms in plan:
        c, a = model.vary(i, v, rng, mode)
        ev.append((onset_s + t_ms / 1000.0, c, a))
    return ev


def plan_end_ms(model: Model, plan) -> int:
    i, t = plan[-1]
    return t + len(model.syls[i].cents)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--provisional", action="store_true",
                    help="use the banks before INGEST1 (build/r3); for testing the page only")
    args = ap.parse_args()
    paths = PROVISIONAL if args.provisional else BANKS
    rng_master = np.random.default_rng(SEED)
    banks = {r: json.loads(p.read_text()) for r, p in paths.items()}
    models = {r: Model(b) for r, b in banks.items()}
    recs = {r: R.recording(r) for r in banks}
    (OUT / "wav").mkdir(parents=True, exist_ok=True)
    screens, key = [], {}
    for sid, part, rec, t0, t1 in PASSAGES:
        m, bank = models[rec], banks[rec]
        rng = np.random.default_rng(rng_master.integers(2**63))
        songs = songs_in(m, bank, t0 + 0.1, t1)
        assert len(songs) >= 2, (sid, songs)
        ref = R.clip(recs[rec], t0, t1)
        n = len(ref)
        busy = [(bank["phrases"][p]["start_s"] - 0.2, bank["phrases"][p]["start_s"] + 5.0)
                for p in range(len(bank["phrases"]))]
        bed = R.noise_bed(recs[rec], busy)
        real = [(s - t0, m.real_phrase(p)) for s, p in songs]

        def render(song_events, harmonics=R.HARM):
            y, mask = R.whistle([e for ev in song_events for e in ev], n, harmonics)
            return y, mask

        def real_events():
            return [events_of(m, plan, on, rng) for on, plan in real]

        versions = {}
        ref_mask = render(real_events())[1]   # where the bird sings (its tracked syllables)
        target = float(np.sqrt(np.mean(ref[ref_mask] ** 2)))
        versions["real"] = render(real_events())
        versions["anchor"] = render(real_events(), R.ANCHOR_HARM)
        if part == "A":
            first = real[0][1]
            for name, v, mode in (("same", 0.0, "shape"), ("nudge1", 1.0, "nudge"),
                                  ("shape1", 1.0, "shape"), ("nudge2", 2.0, "nudge"),
                                  ("shape2", 2.0, "shape")):
                versions[name] = render([events_of(m, first, on, rng, v, mode) for on, _ in real])
        else:
            slots = [(real[k][0], (real[k + 1][0] if k + 1 < len(real) else (t1 - t0)) - real[k][0]
                      - 0.25) for k in range(len(real))]

            def new_songs(order, v=0.0):
                """One new song per real song, lasting within 30 % of it (closest of 400 tries)."""
                out = []
                for (on, room_s), (_, real_plan) in zip(slots, real, strict=True):
                    want = plan_end_ms(m, real_plan) / 1000.0
                    best, best_err = None, np.inf
                    for _ in range(400):
                        plan = m.new_phrase(rng, order=order)
                        dur = plan_end_ms(m, plan) / 1000.0
                        err = abs(dur - want) / want
                        if dur <= room_s and err < best_err:
                            best, best_err = plan, err
                        if best_err <= 0.3:
                            break
                    out.append(events_of(m, best, on, rng, v, "shape"))
                return out
            versions["order2"] = render(new_songs(2))
            versions["order1"] = render(new_songs(1))
            versions["order2s"] = render(new_songs(2, 1.0))
            shuffled = []
            for on, plan in real:
                idx = [i for i, _ in plan]
                calls = {k for k, i in enumerate(idx) if m.syls[i].label.startswith("alarm")}
                free = [i for k, i in enumerate(idx) if k not in calls]  # calls stay in place
                free = [free[j] for j in rng.permutation(len(free))]
                perm = [idx[k] if k in calls else free.pop() for k in range(len(idx))]
                t, sp = 0, []
                for k, i in enumerate(perm):  # the real gaps, in order, between the shuffled syllables
                    sp.append((i, t))
                    if k + 1 < len(plan):
                        gap = plan[k + 1][1] - (plan[k][1] + len(m.syls[plan[k][0]].cents))
                        t += len(m.syls[i].cents) + max(gap, 5)
                shuffled.append(events_of(m, sp, on, rng))
            versions["shuffle"] = render(shuffled)
        out = {"ref": ref}
        for k, (y, mask) in versions.items():
            out[k] = R.match(y, mask, target, bed)
        g = min(1.0, 0.89 / max(float(np.max(np.abs(y))) for y in out.values()))
        for k in out:
            out[k] = out[k] * g
            sf.write(OUT / "wav" / f"{sid}_{k}.wav", out[k].astype(np.float32), R.SR, subtype="FLOAT")
        order = [str(k) for k in rng_master.permutation(list(out))]
        letters = "ABCDEFGHIJ"
        screens.append({"passage": sid, "part": part, "dur_s": round(n / R.SR, 3),
                        "reference": R.C1.flac_b64(out["ref"]),
                        "versions": [{"id": letters[i], "audio": R.C1.flac_b64(out[k])}
                                     for i, k in enumerate(order)]})
        key[sid] = {"part": part, "recording": rec, "span_s": [t0, t1],
                    "songs": [[round(s, 3), int(p)] for s, p in songs],
                    "letters": {letters[i]: k for i, k in enumerate(order)}}
        print(f"{sid}: {len(songs)} songs, {n / R.SR:.1f} s, versions {sorted(out)}")
    # part A first (the same song again), then part B; screens shuffled inside each part
    a = [s for s in screens if s["part"] == "A"]
    b = [s for s in screens if s["part"] == "B"]
    screens = [a[i] for i in rng_master.permutation(len(a))] + [b[i] for i in rng_master.permutation(len(b))]
    page = {"study": "VARY1", "version": 1, "seed": SEED, "minutes": 20, "screens": screens,
            "provisional": bool(args.provisional),
            "parts": {"A": "The same song again: the bird sings one song several times. "
                           "How much does each version sound like this bird singing its song again?",
                      "B": "New songs: the versions sing other songs than the reference. "
                           "How much does each version sound like this bird singing?"}}
    page["design_hash"] = hashlib.sha256(json.dumps(
        [[s["passage"], key[s["passage"]]["letters"]] for s in screens]
        + [{r: b["hash"] for r, b in banks.items()}]
        + [hashlib.sha256("".join(v["audio"] for v in s["versions"]).encode()).hexdigest()
           for s in screens], sort_keys=True).encode()).hexdigest()[:12]
    html = (HERE / "vary1_page.html").read_text()
    html = html.replace("<!--DATA-->", "<script>window.COLOUR1 = " + json.dumps(
        page, separators=(",", ":")) + ";</script>")
    (OUT / "index.html").write_text(html)
    (OUT / "design.json").write_text(json.dumps({
        "study": "VARY1", "design_hash": page["design_hash"], "seed": SEED, "key": key,
        "screen_order": [s["passage"] for s in screens], "provisional": bool(args.provisional),
        "banks": {r: b["hash"] for r, b in banks.items()},
        "models": {r: mm.summary() for r, mm in models.items()}}, indent=1))
    print(f"design {page['design_hash']}: {len(screens)} screens, page {len(html) / 1e6:.1f} MB "
          f"→ {OUT / 'index.html'}")


if __name__ == "__main__":
    main()
