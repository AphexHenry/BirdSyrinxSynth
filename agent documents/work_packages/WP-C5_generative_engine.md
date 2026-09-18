# WP-C5 — Generative performance engine (Leash, Variation, Density, tempo sync, polyphony)

| Field | Value |
|---|---|
| Phase | C — Playability |
| Depends on | C4 |
| Size | L |
| Milestone | – |

## 1. Goal

Let the bird **sing on its own** around the notes the musician holds. A per-voice generative engine chooses syllables made of gestures (archetypes for now), varies them, spaces them in time, optionally locks them to the host tempo, and blends with the player's direct control through **Leash**. Add polyphony ("flock"), stereo spread, seeds and freeze.

## 2. Read first

- `01_research.md` **§7.5–§7.9**
- `02_architecture.md` §7.2
- Reports of WP-C3 and WP-C4

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/control/gesture_engine.hpp` | `GestureEngine` implementing `GestureSource`: syllable scheduler, grammar, variation |
| `core/include/cuicui/control/archetype_grammar.hpp` | built-in grammar over archetypes (weights by Motion/Tone/Density) |
| `core/include/cuicui/control/voice_bank_source.hpp` | **interface stub only** for F3 (`VoiceBankSource` class declared, not implemented) |
| `plugin/Source/VoiceAllocator.h/.cpp` | polyphony, voice stealing, per-voice seeds, stereo spread |
| `plugin/Source/PluginProcessor.*` | tempo info from `AudioPlayHead`, freeze, seed |
| `params.def` | add Layer-3 parameters "Since C5" (§7.2 of architecture) |
| `core/tests/test_gesture_engine.cpp`, `plugin/tests/test_generative.cpp` | tests |

## 4. Specification

### 4.1 Engine state machine (per voice)

```
IDLE --noteOn--> PHRASE_START --> SYLLABLE (sequence of 1–4 gestures) --> GAP --> SYLLABLE ... 
     <--noteOff (finish current gesture with release)--
```

- On noteOn: anchor = note (+bend), phrase seed = `perf.seed` (≠ 0) or a new random seed (0); with probability 0.6 start with 1–3 `intro` gestures.
- **Syllable composition:** choose the syllable's gesture count k ∈ {1..4} with weights shifted by Motion (low Motion → k = 1–2); choose archetypes by weights:
  - base weight table (tunable constants in `archetype_grammar.hpp`),
  - multiply by $\exp(4\,(\text{Motion} - 0.5)\cdot(\text{movement\_score} - \bar m))$ (high Motion favours moving archetypes),
  - multiply buzz/stack weights by $(1 - \text{Tone})$ and whistle by Tone,
  - avoid repeating the same archetype more than twice in a row unless Variation < 0.3 (motif-like repetition).
- **Motif memory:** the first generated syllable sequence (2–5 syllables) is stored as the phrase motif; subsequent syllables repeat the motif with probability $1 - 0.4\,\text{Variation}$, otherwise a new syllable is drawn (zebra-finch-like stereotypy at low Variation).
- **Parameters of each gesture:** default + Variation-scaled random deviation within the archetype's range: $\theta = \theta_{def} + v \cdot \xi \cdot (\theta_{max} - \theta_{min})/6$, ξ ~ N(0,1) truncated ±2.5, v = Variation (0–2).
- **Range** scales pitch depths; **Speed** divides durations and multiplies rates; anchor offsets of syllables relative to the held note: N(0, (150·Range)²) cents, rounded towards scale degrees if Snap > 0.5.
- **Gaps:** log-normal with median $g_{med}$ = (40 + 400·(1 − Density)) ms / Speed, σ_log = 0.35; with probability 0.15·(1 − Density) insert a longer rest (×3).
- **Tempo sync** (`perf.tempo_sync` on, host playing): syllable onsets quantised to the next grid point of `perf.sync_division` using host BPM and ppq position; if the gap would be < 30 ms skip to the following grid point.
- **Freeze:** the current motif (gesture instances with their parameters) is kept and replayed; Variation then only affects micro deviations (±20 % of the normal deviation).

### 4.2 Leash blending (in `VoiceController`, prepared in C3)

- Pitch: $P = (1-\lambda)D + \lambda(P_{anchor} + G_{pitch})$.
- Amplitude/articulation: $u = u_{player}\cdot[(1-\lambda) + \lambda\cdot 10^{G_{amp}/20}\cdot \mathbb 1_{active}]$ (gaps become silences as λ → 1).
- Tone and side: blend numerically with λ; hints apply when λ > 0.5.
- λ changes are smoothed (10 ms).

### 4.3 Polyphony and flock

- `perf.polyphony` voices (1–16); allocation: free voice, else steal the oldest in release, else the oldest.
- Per voice: seed = hash(note, noteOn counter, `perf.seed`); tiny deterministic detune of size (±3 %·Variation) and tone (±0.05·Variation) for flock realism.
- Stereo: constant-power pan per voice spread by `out.stereo_spread` (voice i pan position from a low-discrepancy sequence).
- CPU guard: if the measured block time exceeds 70 % of the block duration for 10 consecutive blocks, stop allocating new voices beyond the current count (report via telemetry).

### 4.4 Real-time constraints

- All gesture curves generated into preallocated pools at syllable start; generating a syllable must take < 0.2 ms (benchmark).
- No allocation, no locks; motif storage fixed-size (max 16 syllables × 4 gestures).

## 5. Suggested steps

1. Engine state machine with archetypes; unit tests in core (headless, deterministic).
2. Leash integration in `VoiceController`; plugin integration; `plugin_render` renders.
3. Motif memory, Variation, Range, Speed, Density.
4. Tempo sync with a fake play head in tests.
5. Polyphony, stereo, CPU guard.
6. Listening renders and human checklist.

## 6. Out of scope

Learned voice banks (F3), custom UI (C6), gesture editing (F4).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Leash = 0 output is bit-identical to the C3 output for the same MIDI, params and seed. |
| 2 | Determinism: `perf.seed` ≠ 0 → identical renders across runs; seed 0 → different phrases per note-on. |
| 3 | Density: syllables per second over 30 s held note increase monotonically for Density 0.2 / 0.5 / 0.8 (report values). |
| 4 | Speed: median syllable duration at Speed 2 is 0.5× ± 15 % of Speed 1. |
| 5 | Variation: with Variation 0 the motif repeats exactly (identical gesture parameters); at Variation 1 the proportion of repeated syllables is 0.6 ± 0.1. |
| 6 | Tempo sync at 120 BPM, 1/16: syllable onsets within ±2 ms of grid points (fake play head test). |
| 7 | Polyphony: 8 held notes at Leash 1, CPU within 8× the single-voice budget × 1.2; voice stealing never produces clicks > −40 dBFS (crossfade 5 ms). |
| 8 | Syllable generation < 0.2 ms (benchmark); zero allocations in `processBlock` over a 60 s generative render. |
| 9 | Freeze: after enabling, the next 10 syllables match the stored motif sequence. |
| 10 | pluginval 5 passes; `scripts/check.sh` passes. |

## 8. Listening & results

`listening/C5/` (via `plugin_render`):

| File | Content | Listen for |
|---|---|---|
| `01_leash_0_to_1.wav` | one note held 20 s, Leash automated 0 → 1 | the bird "wakes up": from steady played note to singing around it |
| `02_motion_low_high.wav` | Leash 1, Motion 0.1 (10 s) then 0.9 (10 s) | calm whistles/stacks → sweeps, trills, chips |
| `03_variation_0_1_2.wav` | 10 s each | stereotyped motif → natural variety → invention |
| `04_density.wav` | Density 0.2 / 0.5 / 0.9 | sparse calls → dense chatter |
| `05_speed.wav` | Speed 0.5 / 1 / 2 | slow, large bird-like phrasing vs rapid |
| `06_tempo_sync_100bpm.wav` | with a click track mixed at −18 dB | syllables lock to the grid |
| `07_flock_chord.wav` | 6-note chord, Leash 1, spread 0.8 | a small flock, spatially spread |
| `08_freeze.wav` | freeze on after 5 s | the same motif repeats like a real zebra finch |
| `09_play_and_let_go.wav` | melody at Leash 0.3 | melody recognisable, with bird "commentary" |

Human checklist: play with Leash on the mod wheel (map it in the DAW), hold chords, try tempo sync in a DAW project. Ask which defaults feel wrong.

## 9. Pitfalls

- Blending pitch at Leash between two independent curves can cause wide swoops when curves diverge a lot; keep the generative anchor tied to the held note and limit anchor offsets by Range.
- Silences from gestures must go through breath (pressure → 0), not through gain — the physical onset is part of the character.
- Tempo information may be absent (Standalone) → free timing.

## Notes from WP-C4

- `ScoreGestureSource` + `ScorePlayer` live in `core/include/cuicui/control/score_player.hpp`. Load a `.gscore.json`, `prepare`, then `process` (0 alloc). Gesture events: leash=1, Motion=0, breath from `settings["perf.breath"]` (default 0.7).
- `generate_archetype` / `cuicui.gestures.score_tools.sequence` build scores. Examples: `examples/scores/*.gscore.json`.
- `movementScore(curves)` is RMS of dP/du in cents — use it to sort archetypes by Motion.
- Side: NaN = unset (jump switcher). Archetypes write a real side; `switch_sweep` is a step. Two-voice uses `secondVoiceCents` (both gates 1).
- Playable anchors at σ=1 still D6–C8. Keep default gallery anchors in that band (stack at D6, switch_sweep from C8).
- Between gestures the player sets gate=false so breath releases physically (not a gain cut).

