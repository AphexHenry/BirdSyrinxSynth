# state.md — live project state

> Updated by agents at the start and end of every WP (see `protocol.md` §2.4 and §5.5). The human may edit it at any time (e.g. change order, mark a WP `optional` or `todo`).

**Last updated:** 2026-10-13 — by: R5 session (R5 done, tag wp-R5; next WP in line: R3; the human wants to improve the performing part next)

## Status legend

| Status | Meaning |
|---|---|
| `todo` | not started; eligible when dependencies are `done` |
| `in progress` | an agent is working on it (or a session was interrupted) |
| `awaiting approval` | finished, waiting for the human's approval to commit |
| `partial` | committed checkpoint; remaining work listed in its report |
| `done` | approved and committed (tag `wp-<ID>`) |
| `blocked` | cannot proceed; reason in "Open questions" |
| `optional` | not selected automatically; human sets it to `todo` to schedule it |

## Execution table

| # | WP | Title | Depends on | Status | Updated | Tag | Spec |
|---|---|---|---|---|---|---|---|
| 1 | A1 | Repository, build system, render CLI, listening kit | – | done | 2026-09-16 | wp-A1 | `work_packages/WP-A1_repo_build_render_listening.md` |
| 2 | A2 | Parameter registry, control signals, oversampling, Python bindings | A1 | done | 2026-09-16 | wp-A2 | `work_packages/WP-A2_params_oversampling_bindings.md` |
| 3 | D1 | Zebra finch dataset ingestion & catalogue | A1 | optional | 2026-09-16 | | `work_packages/WP-D1_dataset_ingestion.md` |
| 4 | B1 | Labial oscillator (normal form) | A2 | done | 2026-09-16 | wp-B1 | `work_packages/WP-B1_labial_oscillator.md` |
| 5 | B2 | Trachea waveguide | B1 | done | 2026-09-17 | wp-B2 | `work_packages/WP-B2_trachea.md` |
| 6 | B3 | Upper vocal tract: glottis–OEC–beak + radiation | B2 | done | 2026-09-17 | wp-B3 | `work_packages/WP-B3_upper_vocal_tract.md` |
| 7 | D2 | Feature extraction (shared real/synthetic) | D1, B3 | optional | | | `work_packages/WP-D2_features.md` |
| 8 | B4 | Respiratory dynamics & noise sources | B3 | done | 2026-09-17 | wp-B4 | `work_packages/WP-B4_respiration_noise.md` |
| 9 | B5 | Two sources: left/right, gating, side switching | B4 | done | 2026-09-17 | wp-B5 | `work_packages/WP-B5_two_sources.md` |
| 10 | B6 | Voice assembly, body size, state save/load, benchmarks | B5 | done | 2026-09-17 | wp-B6 | `work_packages/WP-B6_voice_assembly.md` |
| 11 | C1 | Plugin shell + offline plugin renderer | B6 | done | 2026-09-17 | wp-C1 | `work_packages/WP-C1_plugin_shell.md` |
| 12 | C2 | Source atlas & inverse pitch map | B6 | done | 2026-09-17 | wp-C2 | `work_packages/WP-C2_atlas_inverse_map.md` |
| 13 | C3 | Pitch-led direct playing | C1, C2 | done | 2026-09-17 | wp-C3 | `work_packages/WP-C3_pitch_led_playing.md` |
| 14 | C4 | Archetype library + gesture score format & player | C3 | done | 2026-09-18 | wp-C4 | `work_packages/WP-C4_archetypes_gesture_score.md` |
| 15 | C5 | Generative performance engine | C4 | optional | 2026-10-11 | | `work_packages/WP-C5_generative_engine.md` |
| 15a | R0 | Approach review + understanding studies + web prototype (PITCH1/2, COLOUR1/2, ENGINE1, PROTO1) | C5 | done | 2026-10-11 | wp-R0 | `reviews/2026-10-11_approach_review.md` |
| 15b | R1 | Real syllables + whistle voice + playing modes A/B in the plugin (modular) | R0 | done | 2026-10-12 | wp-R1 | `work_packages/WP-R1_real_syllables_whistle_voice.md` |
| 15c | R4 | Perform UI + presets (revised C6) | R1 | done | 2026-10-12 | wp-R4 | `work_packages/WP-R4_perform_ui_presets.md` |
| 15d | R5 | Melodies that sound right: syllable notes (MELODY2) + held keys | R4 | done | 2026-10-13 | wp-R5 | `work_packages/WP-R5_melodies.md` |
| 15e | R3 | Variations within the bird's style (+ more recordings) | R1, R5 | todo | | | `work_packages/WP-R3_variations_within_style.md` |
| 15f | R2 | Anatomical engine as optional voice (click fix, colour, re-test) | R1 | optional | | | `work_packages/WP-R2_anatomical_voice_optional.md` |
| 16 | C6 | Perform UI, Physiology page, presets v1 | C5 | optional | | | `work_packages/WP-C6_perform_ui_presets.md` |
| 17 | E1 | Fitting framework, losses, identifiability benchmark | D2, B6, C2, C4 | optional | | | `work_packages/WP-E1_fitting_framework.md` |
| 18 | E2 | Voice atlas + Viterbi initial path | E1, C2 | optional | | | `work_packages/WP-E2_voice_atlas_viterbi.md` |
| 19 | E3 | Windowed CMA-ES refinement → gesture scores | E2 | optional | | | `work_packages/WP-E3_windowed_refinement.md` |
| 20 | E4 | Per-bird constants + model adequacy report | E3 | optional | | | `work_packages/WP-E4_bird_constants_adequacy.md` |
| 21 | F1 | Gesture extraction, alignment, clustering | E4 | optional | | | `work_packages/WP-F1_gesture_extraction.md` |
| 22 | F2 | Variation model (FPCA), grammar, voice bank export | F1 | optional | | | `work_packages/WP-F2_variation_model_grammar.md` |
| 23 | F3 | Voice bank runtime integration | F2, C6 | optional | | | `work_packages/WP-F3_voicebank_runtime.md` |
| 24 | F4 | Advanced Gesture page | F3 | optional | | | `work_packages/WP-F4_gesture_page.md` |
| 25 | G1 | Release polish: presets, validation, manual, packaging | R4 | todo | | | `work_packages/WP-G1_release_polish.md` |
| 26 | G2 | Tier-2 physical labial source | B6 | optional | | | `work_packages/WP-G2_tier2_source.md` |
| 27 | G3 | Differentiable twin & amortised inference | E3 | optional | | | `work_packages/WP-G3_differentiable_amortized.md` |

## Current capabilities

**2026-10-13 — WP-R5: melodies that sound right** (done, tag wp-R5).
- **A syllable's note is the pitch of its loud part** (rule E, the loudness-weighted mean; chosen by the human after the blind test MELODY2). Frames more than 20 dB below the syllable's peak are ignored.
  - 11 notes were set by ear in the tuning test TUNE1 (`assets/voicebanks/zf_22938/note_corrections.json`); they override every rule.
  - Rule E agrees with the ear within a semitone for 98 % of the syllables (the old rule 47 %).
- **The bank `zf_22938` is rebuilt** (hash `9e0e08577e5a`): the same 123 syllables, order and curves; only the notes changed.
  - 99 % of playable syllables are within 2 semitones of their loud part (R1: 75 %).
  - Tapped tunes land on their notes, and the "ending goes up" of R1's Frère Jacques is gone.
  - Blind, the new bank beat the old one on 5 of 5 screens (MELODY3: 67 vs 49).
- **New factory preset "Finch, melody"** (the human's choice for held keys): follow the keyboard, Snap to note 100 %, Birds 1. Every syllable of a held key lands on the note.
- **The bank builder** (`python -m cuicui.bank`): `--note-rule` (default E; A rebuilds R1's bank), `--corrections` (format `cuicui.note_corrections` v1).
  - Bank flags `no_note` / `no_tap` keep a syllable from starting a key press, while it still sings in phrases (D-032). The factory bank uses none.
- **The Play page** calls the default note rule "Loud part".
- **Studies with results:** MELODY2, TUNE1, MELODY3 (`agent documents/studies/`).

**2026-10-12 — WP-R4: the instrument window** (done, tag wp-R4).
- **Play page:**
  - a live display shows each syllable's real pitch curve as the bird sings, one colour per bird, over a piano roll; also the held keys' notes and chips naming what sounds now;
  - five cards: Melody (snap to note, first syllable on the note, a syllable's note), Height (size, own height / follow keys), Song (pause, how each key starts), Flock (birds, spread) and Sound (volume, whistle / anatomical, bird harmonics, room).
- **Advanced page:** the anatomical engine's controls, as they were.
- **Keyboard:** a drawn keyboard; a pressed key lights in its bird's colour.
- **Help line:** a sentence for whatever the mouse points at.
- **Presets:** 10 factory presets (Finch, Finch natural / on the note / follow the keyboard, Solo bird, Small flock, Sparse, Chatty, Big bird, Dry). Browse them with the preset bar, save your own (`~/Library/Application Support/Cuicui Instruments/Syrinx/Presets/`). The plugin state remembers the preset and the page.
- **Look:** a designed dark look with the beak's orange as accent; Avenir Next; the window scales 0.75–1.5×.
- **The release call (D-030, the human's rule):** the bird's long rising "alarm" call is kept for the end of a long key press.
  - It never appears in taps or phrases.
  - Letting go of a key that sang at least 0.6 s ends with one call, after the sounding syllable and the bird's own 60 ms silence (at once if between phrases).
  - Otherwise the sound is R1's (the default preset equals no preset, sample for sample).
- **Standalone:** switches MIDI inputs on at first open if none is chosen.
- **Tools:** `plugin_render --preset`, `--screenshot-page`, `--screenshot-scale`; `scripts/validate_plugin.sh 5 --gui`.

**2026-10-12 — WP-R1: the plugin sings with the bird's real syllables** (done, tag wp-R1).
- **Default voice: the whistle.** It is a sine on the real pitch and loudness curves, plus the bird's own harmonics (−30 / −33 dB), with a light room. All 123 real syllables of 22938 in their 12 real phrases are compiled in (`assets/voicebanks/zf_22938/bank.json`).
- **Modes A + B:**
  - tap a key → one real syllable (a random one by default), first syllable exactly on the note;
  - hold → the bird keeps singing its real phrases around the note, with real timing and pauses;
  - release → the current syllable finishes.
- **Controls** (D-025): note rule, own height / follow, size, snap (also on the mod wheel), order (random start / phrase start / shuffled), phrase pause, polyphony (default 4), stereo spread, harmonics, room, master.
- **Identical to the prototype:** on the same MIDI and seed, the plugin output equals a port of the PROTO1 prototype to −138 dB. The prototype page now uses the same bank file.
- **Anatomical engine:** still available as the "Anatomical (experimental)" voice (`voice.type`), unchanged. That path keeps the C3 MIDI tuning and the C5 generator.
- **Tools:**
  - `plugin_render --syllable-log`;
  - `cuicui_core.sing()` (the core whistle path from Python);
  - `python -m cuicui.bank` (build a bank from any annotated recording);
  - the blind test page `listening/R1/mushra/index.html`.
- **Studies with results:** PITCH1, PITCH2, COLOUR1, COLOUR2, ENGINE1, R1CHECK (`agent documents/studies/`). R1CHECK: the plugin playing a real phrase was rated 100 / 100 against the real recording.

## Baselines

| Metric | Value | Measured in | Date |
|---|---|---|---|
| CPU per voice @48 kHz (Release, Apple Silicon) | 2.72 % of one core (RT 0.027); 16 voices RT 0.43 — **above** 0.5 % / 0.1 targets | B6 | 2026-09-17 |
| Plugin CPU @48 kHz / 256 (Release, Apple Silicon) | 2.70 % of one core (~1.0× B6 voice) | C1 | 2026-09-17 |
| Whistle path CPU, 6 keys held, room on @48 kHz / 256 | ≈ 1.15 % of one core (whole plugin) | R1 | 2026-10-12 |
| Tap → sound (whistle) | 36 samples first sample, 53 to > −80 dBFS (latency 35 = 0.73 ms) | R1 | 2026-10-12 |
| Plugin vs prototype (same MIDI, seed) | events identical; pitch ≤ 1.3e-5 c; waveform −138 dB | R1 | 2026-10-12 |
| Whistle clicks (LF at onsets / offsets) | −72.4 / −75.8 dB (real bird −61.0 / −61.8) | R1 | 2026-10-12 |
| Playable syllables within ±2 st of their loud part | 99.2 % (R1 bank 74.6 %) | R5 | 2026-10-13 |
| Note rule within ±1 st of the ear (TUNE1) | rule E 97.6 % (old rule A 47.2 %) | R5 | 2026-10-13 |
| Listener repeatability, tuning by ear | median 32 c (corrected items 45 c) | R5 | 2026-10-13 |
| Blind tune rating, tapped Frère Jacques (MELODY3) | new bank 67.2, R1 bank 49.4 (5 seeds) | R5 | 2026-10-13 |
| Editor CPU, message thread (Retina) | 2.0–2.3 % at rest; 8–10 % while four birds sing (30 fps) | R4 | 2026-10-12 |
| Audio with the editor open + preset switching | 0 allocations; worst block 0.09–0.10 ms of 5.33 ms | R4 | 2026-10-12 |
| Generative density syl/s | 3.23 / 4.50 / 7.00 at Density 0.2 / 0.5 / 0.8 | C5 | 2026-09-18 |
| Flock CPU (8 notes vs 1, generative) | 8.10× (limit 9.6×) | C5 | 2026-09-18 |
| Syllable generate worst | 0.002 ms | C5 | 2026-09-18 |
| Tuning error, tone 0.6, size 1 (median / max cents) | 0.06 / 0.25 (plugin MIDI, D6–C8) | C3 | 2026-09-17 |
| Fitting benchmark (synthetic) median MRSTFT loss | – | E1 | |
| Real zebra finch resynthesis median MRSTFT loss | – | E3/E4 | |

## Known issues

- `clang-format` is not installed; `check.sh` skips the C++ format check (warns). `brew install clang-format` when convenient.
- `uv` is in `tools/uv` (gitignored), not on the default PATH; use the `scripts/` wrappers.
- `Oversampling::decimate` requires `nHost ≤ maxBlockSize` (chunk in the caller).
- B4 item 06 (tension noise vs real alarm) is noisier, not metallic/FM stacked harmonics — see backlog.
- Weak two-side coupling barely locks (Δβ = 0 / 0 / 0.01 at ε = 0.01 / 0.03 / 0.1).
- Shared α cannot span 1.2–6 kHz; listening 09 tops out near 4.6 kHz.
- One voice uses ~2.7 % of a core at 48 kHz OS 4 (planning target 0.5 %). See B6 report §9.
- Auto OS at σ=0.25 is ~4 cents sharp vs OS 8; request OS 8 for tiny-bird sessions (D-015).
- `render_controls` vs CLI differs by 1 ULP (float32 prime vs JSON double).
- Physiology mode still uses C1’s empirical $f_0(\beta)$ table (C4 ≈ 2.5 kHz, velocity shifts pitch). Perform mode is concert pitch.
- Newly copied AU needs `killall AudioComponentRegistrar` before auval sees it (`scripts/validate_plugin.sh` does this).
- C2 playable floor at σ=1 is D6 (~1162 Hz), not C6: the SNIC/onset edge cannot invert to 5 cents on a 0.005 β-grid.
- C2 continuous tone morph (listening 04) at 2 kHz moves ~15–26 cents near whistle; held notes stay ≤ 0.3 cents.
- C3 in-app keyboard is D6–C8. Piano C4–C5 with range_fold sounds as C7 (several keys collapse onto the same bird notes).
- Jumps on D6 (the SNIC/onset floor) sound printer-like. Listening 07 now uses A6–A7–C8. Syllable-shaped breath around switches is C4/C5.
- C4 gallery “high” upsweeps / switch_sweeps can leave D6–C8 and fold.
- C4 hand-designed archetypes match syllable *shapes* better than *colour*. Spectral resemblance to real birds waits on D1 + E (fitting) and possibly G2.
- `perf.voice_bank` = `learned` is a placeholder so auval accepts the enum; it still uses archetypes until F3.
- C5 generative Range can still throw syllable anchors outside D6–C8 at σ=1 and fold.
- **Approach review (2026-10-11):** C4/C5 gestures are not bird-like (on a bare sine they still sound like morse code). Real bird gestures carry the identity. See `reviews/2026-10-11_approach_review.md`.
- Default OEC tracking (`f_H = 3000·(f0/1000)^0.7`) puts the resonance at 1.7–3× f0 (Riede 2006: on f0). This gives too much 2nd harmonic (−11 dB vs the bird's −33 dB) and a synthetic colour.
- Attack clicks: at each onset the labial position jumps from rest (x ≈ 1.0) to the oscillation centre (≈ 0.12) in about 1 ms and leaks into the output.
- Recording `22938` is a finch whose sounds are tonal sweeps at 2.5–4.9 kHz with no low harmonic stacks. `01_research.md` §2.7 zebra-finch priors do not match it.
- **Anatomical engine** (ENGINE1):
  - The clicks are cured offline by articulating with a volume envelope plus a 12 ms pre-roll and 10 ms post-roll, but this is not implemented in code yet (R2).
  - Even fixed, it scores 52 on a passage with a long alarm and low swipes (the whistle scores 100).
  - Score/gesture renders cannot switch off tension noise: `ControlFrame.noise_beta_sigma` defaults to 0.005 in `ScorePlayer`.
- **Pitch tracker** (`scripts/review/track.py`, dominant trace): it hops between the two voices on some syllables. `scripts/studies/voice2.py` tracks the second voice only partially (about 10 % of the time).
- **Prototype pitch feel:** "questionable, maybe fine for now" (human). Pitch at 3–4 kHz is perceptually fuzzy (PITCH1/2); own-height mapping wraps octaves inside a scale (use follow mode for melodies).
- **R1 whistle path:**
  - With "first syllable on the note" (default), the first syllable moves onto the note while the rest of the phrase stays placed around the bird's centre, so it can sit up to an octave away from its natural place in the phrase (prototype behaviour).
  - 28/123 bank syllables are flagged `hop` (> 300 c in 1 ms, tracker voice hops). They are kept.
  - The voice uses D-022's −30 / −33 dB harmonics, while the bank measures −32.7 / −34.2 dB over all syllables.
  - The mod wheel adds to snap in the whistle voice (to Motion in the anatomical voice).
- **R4 editor:**
  - The live display uses 8–10 % of a core while four birds sing (backlog: scrolling-image renderer).
  - Three host parameter names differ from the UI labels ("Snap" / "Snap to note", "Polyphony" / "Birds", "Master" / "Volume"). D-029, backlog.
  - The Standalone's MIDI switch-on was not tried with a MIDI device.
  - D-030:
    - The 0.6 s threshold is fixed (not a control), and a chord let go together gives one call per bird.
    - The PROTO1 web prototype and the Python reference keep the calls inside phrases; reference tests run with D-030 off.
- **Tunes (WP-R5, 2026-10-13):**
  - The notes are fixed (rule E + 11 notes by ear).
  - But a **tapped** tune with one random syllable per key is recognised on some draws only (MELODY3: 2 of 4 answered screens; WP-R5 criterion 3 FAIL). Neighbouring syllables have unrelated shapes, and the steps between notes get lost.
  - For tunes, hold the keys with the preset "Finch, melody". Tap strategies are in the backlog. The human accepted the fail for now.
  - With "Finch, melody", legato tunes are clearer than before but less recognisable than taps ("acceptable for now").
  - Item 11 of the R5 page ends high: its last syllable, #97, was tuned +250 c by ear (+65 c in the check round). Backlog.
- **The bird's centre moved +43 c with the new notes**, so at Snap to note 0 (own height) the whole bird sits 43 c lower than in R1. It is the same bird (criterion 6).
- **Note-rule choices:** "Steady parts", "landing" and "average" also ignore the frames below −20 dB now (median change 20 c).
- **With Birds 1**, a long press's release call (D-030) can overlap the next key's first syllable.
- **Study scripts** in `scripts/review/` and `scripts/studies/` are research code outside the ruff gate. Some lint warnings remain (line length, `zip(strict=)`).

## Open questions for the human

- **Next focus (the human, 2026-10-13):** "i need to move on to improve the performing part". The next WP in the table is R3 (variations + more recordings). Whether a performing-focused WP comes first is the human's call (protocol §3.5 / §8).
- D1 is parked (`optional`) until after Phase B. Recordings live at `/Users/baptistebohelay/Dev/research/birds synth/data/annotations/` (`22938.wav` + `22938.json`; custom tseg JSON schema 1.2). When D1 starts: bird identity, label meanings (`peep`, `swipe`, `peep_grillon`, …), and whether to copy into `data/raw/zebra_finch/`.
- JUCE licence choice before any public release (see `decisions.md` D-003).
- B6 CPU: accept ~3 % per voice, or spend a WP on optimisation (skip gated-off RK4, UVT dirty-flag)? Recommendation: accept for now, revisit if the plugin feels heavy.
- M3 feel: Motion default 0.3, onset, tuning on a real keyboard. Snap useful? Keyboard range D6–C8 OK?
- C4 approved with a colour caveat: hand-designed gestures do not yet resemble real birds. D1 is still parked; resemblance work starts there.
- **Where are the other zebra finch recordings** (and their annotations)? Needed for R3. The human said many are available.
- **`recordings/`** (added 2026-10-12): three MP3s named "… - House Finch" (22937, 44967, 56843). They are for R3 and git-ignored (D-011). Their names say house finch, and 22937 is next to our bank's 22938, which D-023 calls a zebra finch. Which species is 22938, and do these files have annotations (tseg JSON)?
- **R4 play test:** `agent documents/studies/R4PLAY_play_test.md` (Standalone, ~15 min). R4 was approved before it; your answers are still welcome and would feed a polish round (presets, look, wording).
- **Prototype feel:** the human tried it and reported on pitch and fast playing; changes were made (random start, first on note, follow mapping). A further impression is welcome before R1 fixes defaults.
- C5: Leash / Density / Variation defaults; is tempo sync useful in a DAW; should Leash ship mapped to the mod wheel in C6? (superseded by the re-plan)

## Session log (append one line per session)

| Date | WP | Agent/session | Event |
|---|---|---|---|
| 2026-09-16 | – | planning | initial documents created |
| 2026-09-16 | A1 | Cursor Grok 4.6 | started WP-A1 (repo, build, render CLI, listening kit) |
| 2026-09-16 | A1 | Cursor Grok 4.6 | WP-A1 awaiting approval (listening page opened) |
| 2026-09-16 | A1 | Cursor Grok 4.6 | feedback: test.waveform instead of D-013 generator field |
| 2026-09-16 | A1 | Cursor Grok 4.6 | approved, committed and tagged wp-A1 |
| 2026-09-16 | A2 | Cursor Grok 4.6 | started WP-A2 (registry, oversampling, Python bindings) |
| 2026-09-16 | A2 | Cursor Grok 4.6 | WP-A2 awaiting approval (listening page built) |
| 2026-09-16 | A2 | Cursor Grok 4.6 | approved, committed and tagged wp-A2 |
| 2026-09-16 | D1 | Cursor Grok 4.6 | started WP-D1; blocked — `data/raw/zebra_finch/` missing |
| 2026-09-16 | D1 | Cursor Grok 4.6 | human deferred D1 (do B-phase first); recordings path noted; D1 → optional |
| 2026-09-16 | B1 | Cursor Grok 4.6 | started WP-B1 (labial oscillator, parameter guessing, no real audio) |
| 2026-09-16 | B1 | Cursor Grok 4.6 | WP-B1 awaiting approval (listening page opened) |
| 2026-09-16 | B1 | Cursor Grok 4.6 | feedback: t=0 click on silent starts; DC blocker primed; page rebuilt |
| 2026-09-16 | B1 | Cursor Grok 4.6 | approved, committed and tagged wp-B1 |
| 2026-09-16 | B2 | Cursor Grok 4.6 | started WP-B2 (trachea waveguide) |
| 2026-09-16 | B2 | Cursor Grok 4.6 | WP-B2 awaiting approval (listening page opened) |
| 2026-09-16 | B2 | Cursor Grok 4.6 | feedback: trachea too subtle on 02–06; re-rendered stronger examples |
| 2026-09-17 | B2 | Cursor Grok 4.6 | approved, committed and tagged wp-B2 |
| 2026-09-17 | B3 | Cursor Grok 4.6 | started WP-B3 (upper vocal tract: OEC, beak, radiation) |
| 2026-09-17 | B3 | Cursor Grok 4.6 | WP-B3 awaiting approval (M1 listening page opened) |
| 2026-09-17 | B3 | Cursor Grok 4.6 | feedback: sine-like demos, 07/08 motion wrong; listening rebuilt |
| 2026-09-17 | B3 | Cursor Grok 4.6 | 08 metallic/FM noted for B4; rest accepted; awaiting approve |
| 2026-09-17 | B3 | Cursor Grok 4.6 | approved, committed and tagged wp-B3 |
| 2026-09-17 | B4 | Cursor Grok 4.6 | started WP-B4 (respiration & noise) |
| 2026-09-17 | B4 | Cursor Grok 4.6 | WP-B4 awaiting approval (listening page opened) |
| 2026-09-17 | B4 | Cursor Grok 4.6 | feedback: 04–07 listening retuned (pulse, wow, 18 Hz FM, drop M1) |
| 2026-09-17 | B4 | Cursor Grok 4.6 | approved, committed and tagged wp-B4 |
| 2026-09-17 | B5 | Cursor Grok 4.6 | started WP-B5 (two sources, gating, side switching) |
| 2026-09-17 | B5 | Cursor Grok 4.6 | WP-B5 awaiting approval (listening page opened) |
| 2026-09-17 | B5 | Cursor Grok 4.6 | approved, committed and tagged wp-B5 |
| 2026-09-17 | B6 | Cursor Grok 4.6 | started WP-B6 (voice assembly, body size, state, benchmarks) |
| 2026-09-17 | B6 | Cursor Grok 4.6 | WP-B6 awaiting approval (M2 listening page opened; CPU target missed, numbers reported) |
| 2026-09-17 | B6 | Cursor Grok 4.6 | feedback: 03 false A/B pairing; 04 “side” jargon — listening rebuilt |
| 2026-09-17 | B6 | Cursor Grok 4.6 | approved, committed and tagged wp-B6 |
| 2026-09-17 | C1 | Cursor Grok 4.6 | started WP-C1 (plugin shell + plugin_render) |
| 2026-09-17 | C1 | Cursor Grok 4.6 | WP-C1 awaiting approval (pluginval+auval PASS; listening page opened) |
| 2026-09-17 | C1 | Cursor Grok 4.6 | feedback: 02 vel 30 noisy; on-screen MIDI keyboard added; still awaiting approve |
| 2026-09-17 | C1 | Cursor Grok 4.6 | feedback: in-app keyboard C≈D and octave too small; empirical f0(β) invert; still awaiting approve |
| 2026-09-17 | C1 | Cursor Grok 4.6 | approved, committed and tagged wp-C1 |
| 2026-09-17 | C2 | Cursor Grok 4.6 | started WP-C2 (source atlas & inverse pitch map) |
| 2026-09-17 | C2 | Cursor Grok 4.6 | WP-C2 awaiting approval (listening page opened; D6–C8 in tune) |
| 2026-09-17 | C2 | Cursor Grok 4.6 | approved, committed and tagged wp-C2 |
| 2026-09-17 | C3 | Cursor Grok 4.6 | started WP-C3 (pitch-led direct playing) |
| 2026-09-17 | C3 | Cursor Grok 4.6 | WP-C3 awaiting approval (M3 listening page opened; D6–C8 in tune) |
| 2026-09-17 | C3 | Cursor Grok 4.6 | feedback: 07 printer-like; retuned jumps off D6 floor; still awaiting approve |
| 2026-09-17 | C3 | Cursor Grok 4.6 | approved, committed and tagged wp-C3 |
| 2026-09-18 | C4 | Cursor Grok 4.6 | started WP-C4 (archetypes, gesture score, player) |
| 2026-09-18 | C4 | Cursor Grok 4.6 | WP-C4 awaiting approval (listening page opened) |
| 2026-09-18 | C4 | Cursor Grok 4.6 | feedback: 12/13 stack+buzz hiss, two-voice machine; song retuned, still awaiting approve |
| 2026-09-18 | C4 | Cursor Grok 4.6 | approved (colour still not bird-like), committed and tagged wp-C4 |
| 2026-09-18 | C5 | Cursor Grok 4.6 | started WP-C5 (generative engine, Leash, polyphony, tempo sync) |
| 2026-09-18 | C5 | Cursor Grok 4.6 | WP-C5 awaiting approval (listening page opened; pluginval 5 PASS) |
| 2026-09-18 | C5 | Cursor Grok 4.6 | feedback: long glissandi / soft attacks; grammar retuned to short phonemes; page rebuilt |
| 2026-10-11 | C5 | Claude Opus 5.5 | feedback: all items sound like morse code, colour synthetic; approach review + copy-synthesis experiment (`listening/REVIEW/`) |
| 2026-10-11 | C5 | Claude Opus 5.5 | human: real gestures sound like a bird (02/03/05); C5 committed as partial; re-plan pending discussion |
| 2026-10-11 | R0 | Claude Opus 5.5 | PITCH1 / PITCH2 (note of a syllable; dance around the note) done; rule decided (D-019, D-020) |
| 2026-10-11 | R0 | Claude Opus 5.5 | web prototype PROTO1 (A + B with real syllables) built and revised after human feedback |
| 2026-10-11 | R0 | Claude Opus 5.5 | COLOUR1 / COLOUR2 (breath hurts; bird harmonics best) and ENGINE1 (click cause and fix; engine 100/52 vs whistle 100/100) done |
| 2026-10-11 | R0 | Claude Opus 5.5 | human: switch to what works → whistle default voice (D-022); re-plan R1–R4 written; R0 awaiting approval |
| 2026-10-11 | R0 | Claude Opus 5.5 | approved (content committed by the human in `f549ec2`); status done, tagged wp-R0 |
| 2026-10-11 | R1 | Claude Opus 5.5 | started WP-R1 (real syllables, whistle voice, modes A + B in the plugin) |
| 2026-10-12 | R1 | Claude Opus 5.5 | WP-R1 awaiting approval (listening page + blind test page built; criterion 8 waits for the human's blind test) |
| 2026-10-12 | R1 | Claude Opus 5.5 | human: listening page "all sounds like a bird", follow keeps the tune; blind test plugin 100/100 → criterion 8 PASS; still awaiting approve |
| 2026-10-12 | R1 | Claude Opus 5.5 | approved, committed and tagged wp-R1 (D-025…D-027 accepted) |
| 2026-10-12 | R4 | Claude Opus 5.5 | started WP-R4 (Perform UI + presets). Scope agreed: anatomical controls on an Advanced tab, 4 presets + a few + user presets, no MIDI learn, designed look, Standalone first, no mock-up |
| 2026-10-12 | R4 | Claude Opus 5.5 | WP-R4 awaiting approval (criteria 1–8 PASS; listening page built; play test R4PLAY waits for the human; D-028, D-029 proposed) |
| 2026-10-12 | R4 | Claude Opus 5.5 | feedback: the long rising "alarm" only on the release of a ≥ 0.6 s press (D-030); core playing mode + tests; real alarms + demos on the listening page; still awaiting approval |
| 2026-10-12 | R4 | Claude Opus 5.5 | approved, committed and tagged wp-R4 (D-028…D-030 accepted; play test R4PLAY not run before approval) |
| 2026-10-12 | – | Claude Opus 5.5 | MELODY1 (why R1's tapped tune was clear and live playing is not): human listening + analysis. Re-plan (D-031): WP-R5 "melodies" before R3 |
| 2026-10-12 | R5 | Claude Opus 5.5 | started WP-R5 (note rule by blind test MELODY2, bank rebuild, held keys in melodies) |
| 2026-10-13 | R5 | Claude Opus 5.5 | MELODY2 (no pre-registered winner; human chose rule E), TUNE1 (11 notes by ear, E matches the ear 97.6 %), bank rebuilt `9e0e08577e5a`, MELODY3 (new bank beats old 5/5; wide-sweep avoidance not adopted; criterion 3 FAIL), held keys: human chose (c) → preset "Finch, melody"; D-032, D-033 proposed |
| 2026-10-13 | R5 | Claude Opus 5.5 | WP-R5 awaiting approval (criteria 1, 2, 4–7 PASS; criterion 3 FAIL, decision pending; listening page opened) |
| 2026-10-13 | R5 | Claude Opus 5.5 | human: new notes "much better", 14/15 "sound the same", criterion 3 FAIL accepted ("i need to move on to improve the performing part"); D-031…D-033 accepted; committed and tagged wp-R5 |
