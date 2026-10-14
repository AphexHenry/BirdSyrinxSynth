# WP-R6 — Playing feel: the bird follows how you play

| Field | Value |
|---|---|
| Phase | R · Depends on R3 and R5 · comes **after R3** (D-034: the variations can change what is possible here) |
| Size | L (expect two sessions: understanding and prototypes first, then building; a `partial` checkpoint after §4.2 is fine) |
| Milestone | M5' — Syrinx feels like an instrument in your hands and still sounds like the bird |

## 1. Goal

The human, 2026-10-13: *"give to the user a better feeling of performance. what we aim for is to keep the bird natural but still match the keyboard expressivity. it should organically follow different mode of playing: pizzicato, long notes, give a feeling of control while keeping the organic nature of birds. that's a very tricky challenge that we need to tackle with care, understanding and out of the box fresh ideas."*

So:
- **What the player does should be heard:** short or long, soft or hard, fast or slow, detached or legato, repeated notes, chords, pedal, wheel.
- **What is heard should stay a real bird:** real syllables, the bird's own timing and statistics, no synthetic sustained tones.
- **The player should feel in control:** predictable onsets, notes and loudness, and the bird's own life on top.

## 2. What we know (start from here)

**The bird is pizzicato by nature** (bank `zf_22938`, measured 2026-10-13):
- syllables are short: swipe median 86 ms (51–152), peep 76 ms (51–164), swipe+swipe 189 ms;
- its only long sounds are the 5 rising calls (379–441 ms, D-030);
- it has no held, steady notes (6 % of frames are steady: approach review);
- inside a phrase it sings about 7 syllables per second, with gaps of 36 ms median (13–91 ms);
- phrases last 0.26–3.08 s, with pauses of 2.4 s median (0.2–6.1 s) between them.

A keyboard "long note" therefore has no single-syllable equivalent: the bird must *do something* for as long as the key is held.

**What the keys do today:**
- a tap gives one syllable, its note on the key (R1, R5);
- a held key: the bird continues its real phrases around the note (Snap to note, mod wheel);
- release: the sounding syllable finishes; after a press of 0.6 s or more, the long rising call (D-030);
- velocity changes **only** the loudness (0.12 + 0.28·v^1.5);
- the pedal holds; "Finch, melody" puts every syllable on the note, one bird (D-033).

**What the human has heard so far:**
- taps keep tunes clearer than held keys (MELODY1, WP-R5);
- legato with "Finch, melody" is "not as recognisable as the tap. acceptable for now";
- tapped tunes depend on which syllables are drawn (WP-R5 criterion 3, accepted fail; tap strategies in the backlog);
- the R4 play test (`studies/R4PLAY_play_test.md`) has not been run yet.

## 3. Read first

- This spec. The approach review (`reviews/2026-10-11_approach_review.md`): the playing-modes decision (item 7) and "Approach ideas".
- Decisions D-020, D-021 (modes A + B, modular for C/D/E), D-025, D-030, D-031–D-034.
- The reports of R3 (variation controls, new material: what R6 can drive), R5 (§10, §12) and R4 (§12: Play page, presets, feed).
- Studies: MELODY1, MELODY3 (reading), R4PLAY.
- Code: `core/include/cuicui/control/playing_mode.hpp`, `mode_ab.hpp`, `phrase_order.hpp`, `syllable_engine.hpp`, the plugin's `processWhistle`. Also the PROTO1 web prototype (`scripts/studies/proto1_page.html`), which the human plays in Chrome with the MIDI keyboard.

## 4. Approach: understand, try, then build

The human asked for care, understanding and fresh ideas. Do not design the behaviour from a one-line wish; earlier attempts that did drifted (R0 "morse code"). Work in three stages, each ending with the human.

### 4.1 Understand (before any design)

- **Scoping questions** (numbered, lettered options, the human answers in one line):
  - which ways of playing matter most and in what order: pizzicato, long notes, legato lines, repeated notes, trills, fast runs, chords, dynamics, pedal;
  - which controllers their keyboard has and they use: velocity, aftertouch, mod wheel, pitch bend, expression or sustain pedal;
  - what "control" means to them: onset, note, loudness, length, ending?
  - what must never happen (e.g. mechanical repetition, lag, the bird ignoring a key).
- **The human's own playing:**
  - the human plays 5–10 short sketches (pizzicato, long notes, a legato line, repeated notes, a fast run…) on the MIDI keyboard into the Standalone or the prototype, with one comment per sketch;
  - capture the MIDI (a recorder in the prototype page, or a host);
  - the sketches become the test material for everything after: the same gestures, played through every behaviour.
  - Run R4PLAY in the same sitting if it is still open.
- **Two vocabularies, side by side** (a short study doc with numbers):
  - the player's: note lengths, gaps between notes, velocities, overlaps, repeats, chord sizes;
  - the bird's (§2: syllable lengths and types, rates, gaps, pauses, loudness range).

  The design maps one onto the other.

### 4.2 Fresh ideas → playable prototypes

These are hypotheses to try, not a list to implement. Each must be audible on the human's sketches **and** playable live.
1. **Long notes, the bird's way.** Behaviours to compare:
   - a **trill**: one syllable repeated at the bird's own rate, on the note, slowing or speeding;
   - a **phrase** around the note: today's B;
   - the **long call**: today, on release;
   - a **stretched** syllable, where it has a steady part;
   - a mix that changes as the key stays down.
2. **Start short, then decide.** At key-down the length of the note is unknown, so the first syllable must start at once, on the note. What follows depends on how long the key stays down: one syllable for pizzicato; the long-note behaviour beyond a hold time.
3. **Velocity shapes character, not only loudness:**
   - soft → peeps, narrow sweeps, close to the note;
   - hard → swipes, wider sweeps, brighter (harmonics), maybe the louder syllables.

   Choose from the real syllables.
4. **The player's tempo sets the bird's tempo.** The gaps between the player's notes set the density of a held note's syllables (fast playing → short syllables, close together; slow → longer ones, more air). The bird's own rates and gaps bound it.
5. **Legato that slurs.** For a step up, choose a syllable that sweeps up; for a step down, one that sweeps down. The bird's shape then carries the line. This may also help tapped tunes (backlog, R5).
6. **Repetition as a tool.** Repeating a key repeats the same syllable, as birds do. That gives rhythm and exact intervals (backlog idea from R5: keep the syllable along a line).
7. **Endings as gestures:** let it ring, cut it short (a quick release), or close with the call (D-030); maybe release velocity.
8. **Continuous controllers:**
   - aftertouch or expression → trill rate, swell, or "agitation" (R3's variation amount);
   - pitch bend → the whole bird;
   - mod wheel → Snap to note (today).
9. **Organic, but predictable where it matters:** the first syllable always exactly on key-down and on the note. Natural timing and variation (R3) come after it, scaled by a "tame ↔ wild" control.
10. **A breath budget.** A bird cannot sing forever: long holds breathe (pauses drawn from the bird's own statistics), and the player's rests refill the budget.
11. **Fresh ideas from the sketches themselves** (the agent's own, argued from §4.1's numbers).

Prototype where the human can play and switch behaviours live: the web prototype (PROTO1 style, MIDI in Chrome), or switchable options in the Standalone. Render every sketch through every candidate behaviour for A/B listening.

### 4.3 Decide, then build

- **The human chooses**, from the prototypes and the renders of their own sketches.
- **Blind tests** settle questions of sound, pre-registered, short: "which sounds more like the bird" (against the recording) and "which follows my playing" (the human's sketches).
- **Build the chosen behaviours as a playing mode** (D-021: a new `PlayingMode`, or an evolution of `ModeAB`):
  - its parameters and presets each need a decision entry;
  - real-time safe, deterministic, with tests.

## 5. Deliverables

1. The scoping answers, at the top of the report.
2. The human's sketches: small MIDI files plus comments, committed (e.g. `assets/midi/r6_sketches/`), and the vocabulary study doc.
3. The prototype(s) and the renders of every sketch through every candidate behaviour.
4. The pre-registered studies that were needed, with results.
5. The chosen behaviours in the plugin: playing mode, parameters, presets (e.g. "Finch, pizzicato" or "Finch, long notes", if the human wants them), with tests.
6. The listening page, report, state, decisions and backlog.

## 6. Tests & acceptance criteria (to refine with the human at scoping; record the final list in the report)

| # | Criterion |
|---|---|
| 1 | **Play test** by the human (their MIDI keyboard, Standalone; checklist written before): each way of playing chosen at scoping is rated "I feel in control" **and** "it sounds like the bird", or the gaps are recorded and accepted |
| 2 | **Birdness kept:** an R1CHECK-style blind test, with natural phrases played through the new mode, scores ≥ 90 against the recording |
| 3 | **Responsiveness:** the first syllable starts on the key-down sample, with latency unchanged (R1: 0.73 ms) |
| 4 | Real-time safety and determinism: 0 allocations on the audio thread; output independent of the block size; CPU within the R1/R4 baselines (reported) |
| 5 | Each chosen behaviour has tests (e.g. a tap of 60 ms gives one short syllable; a 3 s hold follows the chosen long-note behaviour with gaps within the bird's range) |
| 6 | The human's sketches before and after on the listening page; `scripts/check.sh` and `scripts/validate_plugin.sh 5 --gui` pass |

## 7. Listening & results

`listening/R6/`:
- each of the human's sketches: today, each candidate behaviour, and the chosen one;
- per way of playing (pizzicato, long notes, legato, repeats, fast runs, dynamics), the renders that show it best;
- the blind tests' pages.

## 8. Out of scope

- New syllable shapes or variation models: R3. R6 drives what R3 provides.
- The anatomical voice (R2).
- Modes C (conductor/flock), D (drawn gestures), E (whistle-in): later WPs, though R6's design should leave room for them (D-021).
- Release polish (G1).

## 9. Pitfalls

- **Control against nature.** Every behaviour uses real bird material within the bird's statistics; no synthetic held tones.
- **Evidence, not one-line feedback.** Use renders of the human's own sketches, live prototypes and short pre-registered blind tests (the memory "listening evidence first").
- **Note-on knows nothing about the length.** Never wait to decide: start the first syllable at once and adapt afterwards.
- **References must stay exact.** If syllable choice changes, keep the R1 / PROTO1 reference playing (the `release_alarm` off path) unchanged, or put the new behaviours behind settings. `test_r1_whistle.py` must stay exact.
- **Interplay with what exists:** the D-030 call, "Finch, melody" (Birds 1, Snap 100 %), the D-032 `no_tap` flags, the mod wheel on Snap, polyphony and the release of the oldest key.
- **The keyboard itself:** velocity curves differ between keyboards, and aftertouch may be missing. Ask at scoping, and make the mapping adjustable if needed.

## Notes from WP-R3

- **Two birds now (D-036).**
  - The plugin sings `hf_22938` (436 syllables, the whole recording) + `hf_22937` (358), merged when it starts. The files stay separate.
  - `SyllableBank::birds()` names them, and each syllable knows its `bird`.
  - `useBankFiles(paths)` / `plugin_render --bank` sing one bird alone, or the R5 bank (`zf_22938`).
- **Variation per request** (D-036, amended after STRETCH1). `SyllableRequest` carries:
  - **`lengthScale`**: time stretch, 1 = as sung;
  - **`rangeScale`**: the pitch movement around the note, 1 = as sung;
  - `variation` (the amount they were drawn with), `variantSeed`;
  - `shape` (the bird's shape model, 0: not audible, VARY2).

  ModeAB draws the scales per syllable with `control::drawStretch` within `PlayContext::spans`: length 80–160 % (labels with "swipe": up to 200 %) and range 60–160 %, the spans the human chose by ear in STRETCH1, scaled by the Variation knob. It schedules the next syllable after the stretched length (`stretchedLengthMs`), so the bird's real gaps stay.
  - A new playing mode can set the scales itself, e.g. a held key → longer syllables, velocity → a wider sweep.
  - The engine renders any values (long stretches are clamped to `SyllableEngine::kMaxVariantMs` = 2048 ms). Beyond the human's spans is untested.
- **Finding syllables.** `SyllableBank::find(Query)` filters by label prefix ("peep", "swipe", "alarm"…), length (ms), sweep (end − start pitch, cents) and bird. It allocates, so call it off the audio thread and keep the lists.
  - Per syllable you also get `lengthMs`, `sweepCents`, `peakDb`, `type` (cluster of renditions within its bird) and `model`.
  - The labels of the new syllables are proposals (about 2 in 3 right for swipe vs peep). Length and sweep are measured, so they are more reliable.
- **The bird's vocabulary** (both birds): syllables 50–160 ms (median ≈ 85), gaps between syllables ≈ 41 ms ± 4 ms within a song, songs 1.5–4 s.
  - 22937 sings fixed songs (4 song types, the same order every time).
  - 22938 branches: every song starts the same way, then takes one of a few paths.
  - The long rising calls (D-030) are the song endings. They sound thinner than the bird (INGEST1, backlog).
- **New order was not adopted** (the human, 2a). Phrases are the real ones. `cuicui.variation.Model.new_phrase` exists in Python if R6 wants to try recombining at branch points.
