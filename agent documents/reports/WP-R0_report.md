# WP-R0 report — Approach review, understanding studies, web prototype

| Field | Value |
|---|---|
| WP | WP-R0 (no spec file: requested by the human in chat, 2026-10-11) |
| Status | done (tag `wp-R0`) |
| Started / finished | 2026-10-11 / 2026-10-11 |
| Agent | Claude Opus 5.5 (one long session) |
| Commits | `5f22e66` (review, PITCH1/2, COLOUR1/2, study scripts) and `f549ec2` (ENGINE1, R1–R4 specs, D-018…D-024, study data), both by the human; closing commit tagged `wp-R0`. |

## 1. Summary (plain language)

The human rejected C5 ("morse code") and asked for a research-director review. The review found the root cause: the bird lives in its **gestures**, and ours were invented. Played on a plain sine, the bird's real pitch and loudness curves "really feel like a bird".

We then ran six short, blind, pre-registered listening studies with the human as the listener. They settled:
- what "the note" of a syllable is;
- how notes relate to the keyboard;
- which colour ingredients matter;
- whether the anatomical engine can be cleaned up.

A playable **web prototype** (modes A + B with all 123 real syllables) now exists. Following the human's decision, the **default voice becomes a whistle with the bird's own harmonics**, which is non-anatomical; the anatomical engine becomes optional. The plan continues with **R1** (bring this into the plugin).

## 2. What was built (no plugin or core code changed)

| File / folder | Purpose |
|---|---|
| `agent documents/reviews/2026-10-11_approach_review.md` (+ `assets/`) | The review: measurements, the 2 × 2 copy-synthesis experiment, diagnosis, human verdicts (§8), click diagnosis (§9), human decisions (§10) |
| `agent documents/studies/PITCH1_perceived_pitch.md` | Which pitch is "the note" of a syllable (method of adjustment) |
| `agent documents/studies/PITCH2_dance_around_the_note.md` | Phrases vs singles, steady vs landing, with a held note 2–3 octaves below |
| `agent documents/studies/COLOUR1_what_makes_it_real.md` | MUSHRA: harmonics, breath, room, second voice, engine |
| `agent documents/studies/COLOUR2_gentler_colour.md` | Graded breath and harmonic levels vs the pure sine |
| `agent documents/studies/ENGINE1_engine_cleanup.md` | Engine parameter sweep, click cause and offline fix, listening check |
| `agent documents/studies/data/*/` | Raw ratings and hidden keys of every study (small JSON) |
| `agent documents/studies/assets/` | Study figures |
| `agent documents/work_packages/WP-R1…R4_*.md` | The re-plan: R1 detailed, R2–R4 brief |
| `agent documents/decisions.md` D-018…D-024 | The human's decisions from this session |
| `scripts/review/` | `track.py` (dominant-trace f0 / amplitude tracker and metrics), `experiment.py` (2 × 2 copy-synthesis), `spec.py` (spectrograms) |
| `scripts/studies/` | Generators and analyses for each study (`*_make.py`, `*_analyze.py`, pages `*_page.html` / `pitch2_*.{html,js}`); `proto1_make.py` + `proto1_page.html` (the prototype); `voice2.py` (partial second-voice tracker); `engine_sweep.py`, `engine_gate_poc.py` |

## 3. How to run it

```bash
python/.venv/bin/python scripts/studies/proto1_make.py      # rebuild the prototype → listening/PROTO1/index.html
open -a "Google Chrome" listening/PROTO1/index.html            # Chrome for a MIDI keyboard
python/.venv/bin/python scripts/review/experiment.py scripts/review "$PWD" listening/REVIEW   # 2 × 2 copy-synthesis
python/.venv/bin/python scripts/studies/colour1_make.py       # any study: *_make.py builds the page, *_analyze.py <results.json> analyses it
```

- **Recording path:** the scripts read the recording at `/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav` (+ `.json` tseg annotations).
- **Generated output:** pages and renders go to `listening/` (git-ignored).
- **Pages that need a local web server:** the MUSHRA pages are about 2–4 MB. Real browsers open them from file://. The Claude in-app browser needs a local server (`python3 -m http.server`).

## 4. Results (each study doc has the full pre-registered analysis)

| Study | Key result |
|---|---|
| Review 2 × 2 | Real gestures on a sine "really feel like a bird"; C5 gestures on the same sine sound like "morse code". The engine with OEC on f0 is "perfect apart from attack clicks". |
| PITCH1 | Steady-tone matching scatter is ±151 c at 3.4 kHz; real syllables are vaguer. The note sits nearer the end; turning points do not define it. |
| PITCH2 | With a held note 2–3 octaves below, placement barely matters: P(yes) 0.79 on the note vs 0.65 a tritone away. No rule preference measurable. |
| COLOUR1 | Real recording found 3/3. Plain sine 94; breath hurts (+10 / +32 / +8 when removed); engine 44; buzzy anchor 25. |
| COLOUR2 | Breath: the louder, the worse. Harmonics never hurt; the bird's own level (−30 dB) is the best synthetic version (98). |
| ENGINE1 | Parameters fix breath and most brightness, not clicks. The offline click fix brings all click measures to the real bird's level. Listening: fixed engine 100 / 52, whistle 100 / 100. |

## 5. Decisions (human, see `decisions.md`)

- **D-018:** pivot to real gestures; understand before building.
- **D-019:** note rule.
- **D-020:** mapping and placement.
- **D-021:** modes A + B, modular.
- **D-022:** whistle is the default voice.
- **D-023:** 22938 is a zebra finch; more recordings exist.
- **D-024:** plan R0 → R1 → R4 → R3, R2 optional.

## 6. Known issues

See `state.md` → Known issues (engine P3 failure, tracker voice hops, prototype pitch feel, lint in research scripts).

## 12. Handoff notes for the next agent (R1)

- **Start from the prototype.** `scripts/studies/proto1_page.html` is the reference implementation of modes A + B, placement and voice. `proto1_make.py` builds the bank. R1 ports these to C++ in the plugin (see `WP-R1`).
- **Listen to the human.** The human's ear decides. Every voice or playing change ends with a short MUSHRA check against the real recording; reuse `scripts/studies/colour1_page.html` (generic) and the `engine1_make.py` pattern.
- **Keep it simple.** Pitch at the bird's register is fuzzy, so do not over-engineer tuning. Breath noise is out, and so is the anatomical engine as default.
- **Talk in plain language.** The human prefers short explanations (they once asked "rephrase simpler"), progressive steps, and isolating one factor at a time.

## 15. Proposed commit message

```
WP-R0: approach review, listening studies, web prototype, re-plan

Review of the approach after C5 was rejected; six pre-registered listening
studies (pitch rule, mapping, colour, engine clean-up) with the human as
listener; a playable web prototype of modes A+B with the bird's real
syllables; decisions D-018..D-024 and work packages R1-R4.
No plugin or core code changed.
Report: agent documents/reports/WP-R0_report.md
```
