# WP-C4 — Archetype library + gesture score format & player

| Field | Value |
|---|---|
| Phase | C — Playability |
| Depends on | C3 |
| Size | M |
| Milestone | – |

## 1. Goal

Create the vocabulary of songbird sounds before any data-driven learning: a library of **hand-designed archetypes** (whistle, sweeps, trill, buzz, harmonic stack, chip, two-voice note, side-switch sweep, intro note…), the **gesture** data structure, and the **gesture score** file format with a player that renders both dense physiological tracks and gesture events. These are the building blocks the generative engine (C5) and the voice bank (F-phase) will use.

## 2. Read first

- `01_research.md` **§7.7–§7.9**, §7.3
- `02_architecture.md` **§9.4** (gesture score), §6
- Report of WP-C3 (`GestureSource` interface, controller behaviour)

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/control/gesture.hpp` | `GestureCurves` (4 channels × 64 samples), `GestureHints`, `GestureInstance`, evaluation (cubic Hermite in u) |
| `core/include/cuicui/control/archetypes.hpp` | archetype parameter structs, defaults, ranges, `generate(...)` |
| `core/include/cuicui/io/gesture_score.hpp` | JSON read/write (offline) |
| `core/include/cuicui/control/score_player.hpp` | plays physio tracks and/or gesture events into `VoiceController`/`BirdVoice` |
| `schemas/gesture_score.schema.json` | schema |
| `apps/bird_render` | `bird_render <file.gscore.json> -o out.wav` |
| `python/bindings/module.cpp` | `archetype_names()`, `archetype_params(name)`, `generate_archetype(name, params, seed)`, `render_score(score: dict, sample_rate) -> audio` |
| `python/cuicui/gestures/score_tools.py` | helpers to compose scores in Python (sequence of archetypes → score dict) |
| `examples/scores/*.gscore.json` | example scores |
| `core/tests/test_archetypes.cpp`, `test_gesture_score.cpp`, `python/tests/test_scores.py` | tests |

## 4. Specification

### 4.1 Gesture representation

- Channels on u ∈ [0,1], N = 64 samples: `pitch_cents` (relative to anchor), `amp_db` (≤ 0, peak 0), `tone` [0,1], `side` [−1,1].
- `GestureInstance {tStart, durationMs, anchorCents, curves, hints}`; hints: `oecRatio`, `gape`, `secondVoiceCents` (NaN = unset).
- Evaluation: cubic Hermite (monotone, Fritsch–Carlson) between samples; `side` uses linear interpolation (to keep switches sharp when the curve has a step).

### 4.2 Archetypes (all curves generated into fixed arrays; seeded randomness only through parameters)

Amplitude envelope helper `env(u; a, r)`: raised-cosine attack over fraction a, release over fraction r, 0 dB plateau, −60 dB at the ends.

| Name | Parameters (default; range) | Curves |
|---|---|---|
| `whistle` | dur 250 ms (80–800); arch 60 cents (−300–300); tone 0.9 | P = arch·4u(1−u); amp env(0.1, 0.15); side −1 |
| `upsweep` | dur 120 (30–400); depth 900 (100–2400); curvature 2 (−6–6); anchor_at end | $s(u) = (1 - e^{-ku})/(1 - e^{-k})$ (k→0: u); P = −depth·(1 − s(u)); tone 0.8 |
| `downsweep` | dur 120; depth 900; curvature 2; anchor_at start | P = −depth·s(u); tone 0.8 |
| `arch` | dur 180; depth 500 (−1200–1200); sharpness p 1.5 (0.5–4) | P = depth·sin(πu)^p |
| `trill` | dur 400 (100–2000); rate 22 Hz (6–40); element depth 400 (50–1500); duty 0.7 | n = round(rate·dur); element phase φ = frac(n u); P = depth·(1 − φ) smoothed (raised-cosine edges 10 %); amp gated by duty per element |
| `buzz` | dur 200 (50–800); wobble 30 cents; tone 0.05 | P = seeded smooth random walk (low-pass 20 Hz) ±wobble; tone 0.05 (→ roughness coupling in C3) |
| `stack` | dur 150 (60–400); droop −30 cents; tone 0.15; oec_ratio 5 (2–10) | P = droop·u; hint oecRatio |
| `chip` | dur 20 ms (8–40); depth 700; sharpness 2 | P = depth·(1−u)^sharpness; amp env(0.05, 0.4) |
| `two_voice` | dur 200; interval 380 cents (−1200–1200); tone 0.7 | P flat; hint secondVoiceCents = interval; side 0 |
| `switch_sweep` | dur 150; depth 2000 (downward); crossover u 0.5 | P = depth·(1 − u) − depth; side = +1 for u < crossover, −1 after (step) |
| `intro` | dur 60 (30–120); depth 300; tone 0.35 | P = depth·(1 − u)² ; amp env(0.1, 0.3) |

Each archetype also exposes a `movement_score` (RMS of dP/du normalised) used by C5.

### 4.3 Gesture score player

- **Physio tracks:** sampled at `control_rate_hz`; each tick builds a `ControlFrame` from tracks (missing → settings/defaults) and calls `BirdVoice::process` for the corresponding host samples (linear ramp between ticks). Must be bit-identical to `render_controls` with the same arrays.
- **Gesture events:** a `ScoreGestureSource` (implements `GestureSource`) outputs the active gesture's sample at each tick; between gestures `active = false` → pressure 0 (silence with physical release). The controller runs with λ = 1 (pitch from gestures, anchor = `anchor_hz`), breath = score setting (default 0.7), Motion = 0 (no extra ornaments).
- Both sections present → physio tracks drive physiology, gesture events are ignored with a warning (documented rule).

## 5. Suggested steps

1. Gesture structures + evaluation + tests.
2. Archetypes + curve tests + Python gallery plots.
3. JSON I/O + schema + round-trip tests.
4. Score player (physio, gestures), `bird_render` support, bindings.
5. Example scores, listening page.

## 6. Out of scope

Random phrase generation, grammar, Leash, Variation, polyphony (C5); learned gestures (F-phase); UI (C6/F4).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Every archetype at defaults and at 50 random parameter sets within range: finite curves; amp peak = 0 dB ± 0.01; tone ∈ [0,1]; side ∈ [−1,1]; pitch jumps between adjacent samples < 400 cents except `switch_sweep` side step and trill element resets (documented exceptions). |
| 2 | Determinism: same params + seed → identical curves; `buzz` differs with seed. |
| 3 | JSON round trip C++ write → read → write is byte-identical; Python `jsonschema` validates all example scores; invalid scores produce clear errors. |
| 4 | Physio-track playback bit-identical to `render_controls` for the same arrays. |
| 5 | Gesture-event playback follows the intended pitch: median absolute error between f₀ and target ≤ 15 cents over voiced frames of `whistle`, `upsweep`, `downsweep`, `arch` (tone ≥ 0.6); the two-voice archetype shows both pitches (D2 two-voice detector flags it). |
| 6 | Trill at 22 Hz: measured element rate within ±5 %. |
| 7 | Zero allocations when playing a score in real time (player used inside the processor test harness). `scripts/check.sh` passes. |

## 8. Listening & results

`listening/C4/`:

| File | Content | Listen for |
|---|---|---|
| `01_gallery_<archetype>.wav` (one per archetype) | 3 variants each (defaults, low, high parameters) separated by 300 ms | each archetype is recognisable and bird-like |
| `12_song_from_archetypes.wav` | example score: 3 intro notes, stack, downsweep, trill, buzz, two-voice note, chip pair | a plausible songbird phrase built only from archetypes |
| `13_same_song_size2.wav` | same score at σ = 2 | bigger bird |
| `14_physio_score.wav` | B6 demo phrase converted to a physio-track score | identical to the B6 render |

Figures: curve plots of each archetype (pitch/amp/tone/side) with spectrograms.

Ask the human which archetypes sound least convincing (to prioritise tuning in C5 or later).

## 9. Pitfalls

- Pitch curves for sweeps must be defined relative to a sensible anchor (`anchor_at`) so that musicians' notes land where expected.
- Don't allocate `std::vector` per gesture in real time: fixed-size arrays.

## Notes from WP-C3

- `GestureSource` / `NullGestureSource` / `GestureSample` live in `core/include/cuicui/control/gesture_source.hpp`. Hint fields (`oecRatio`, `gape`, `secondVoiceCents`) use NaN = unset (`hintSet()`).
- `VoiceController::tick` expects `out` to be a **fresh host ControlFrame** each call (oec/gape/noise bases). Do not feed back the previous output or tracking compounds.
- Snap weights |dP/dt| of the **unsnapped** pitch (gesture/direct), then pulls toward `quantizeCents`.
- Perform vs physiology: `perf.mode` 0 = controller, 1 = C1 empirical MIDI table. Default is perform.
- Playable concert pitch at σ=1 is D6–C8 (MIDI 86–108). `setGestureSource` is how C4/C5 plug in; Leash is still 0 until C5 adds `perf.leash`.
- Side-switch jumps at the D6 floor are noisy. Archetypes should include amplitude/breath envelopes around jumps so they read as syllables, not mechanical gates.

