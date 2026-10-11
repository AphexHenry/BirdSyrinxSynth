# WP-R1 — Real syllables, whistle voice and playing modes A + B in the plugin

| Field | Value |
|---|---|
| Phase | R (re-plan after the 2026-10-11 approach review) |
| Depends on | R0 (approach review and studies, committed) |
| Size | L (likely 2 sessions; a `partial` checkpoint is fine) |
| Milestone | M4' — the plugin sings like the real bird, playable from MIDI |

## 1. Goal

Bring what the studies proved into the plugin. The plugin must play **your zebra finch's real syllables** with the **whistle voice** (a sine following the real pitch and loudness curves, plus the bird's own harmonics). It plays through **playing modes A + B**:
- tap a key → one syllable;
- hold → the bird keeps singing its real phrase around the note;
- release → it finishes the syllable.

Build it **modular**, so modes C (conductor/flock), D (drawn gestures) and E (whistle-in) can be added later without rewriting.

The **web prototype is the executable specification.** `listening/PROTO1/index.html` is built by `scripts/studies/proto1_make.py` from `proto1_page.html`; it was played and adjusted by the human. When in doubt, do what the prototype does.

## 2. Read first

- `agent documents/reviews/2026-10-11_approach_review.md`, especially §8–§10 (listening results and the human's decisions).
- `agent documents/studies/*.md` (PITCH1, PITCH2, COLOUR1, COLOUR2, ENGINE1): results sections only.
- `decisions.md`: D-018 to D-024.
- The prototype's JS (`scripts/studies/proto1_page.html`), in particular the `Bank`, `Placement`, `Voice`, `Orders`, `Singer` and `Performer` modules.
- The C5 report's handoff notes (`reports/WP-C5_report.md` §12): `VoiceAllocator`, polyphony, tempo and the zero-allocation rules are reused.
- `02_architecture.md` §5 (real-time rules), §9.5 (voice bank format), §12 (git).

## 3. Deliverables

1. **Bank export** (Python, extends `scripts/studies/proto1_make.py`).
   - Writes a versioned voice-bank asset, e.g. `assets/voicebanks/zf_22938/bank.json`. Keep it small (about 0.2 MB) and commit it as an asset; it is derived contours, not audio.
   - Contents: all voiced syllables of `22938`, grouped into the bird's real phrases (gap > 150 ms = new phrase), with real onset times. Per syllable:
     - 1 ms pitch (Hz) and amplitude curves;
     - label;
     - anchors for **rule** (longest steady part, else ending), **steady**, **landing** and **average**;
     - the bird's centre per rule.
   - The bank-building code must stay species- and recording-agnostic: more recordings come later.
2. **Core (C++, `core/`, no JUCE):**
   - `VoiceBank`: load and validate the bank. Never on the audio thread.
   - `Placement`, with the human's decisions D-019 and D-020:
     - the syllable's note by rule; default "longest steady part (±50 c, ≥ 20 ms), else ending";
     - target note by mapping: **own height** (default; note name at the octave nearest the bird's centre) or **follow the keyboard** (+36 semitones);
     - **size** transposition;
     - **first syllable exactly on the note**;
     - following syllables placed by **snap** (0 = natural melody … 1 = all on the note).
   - `PhraseOrder` strategies:
     - **random start** (default): a random syllable of the bank, then the rest of its real phrase, then pause, then a random next phrase;
     - **phrase start**;
     - **shuffled**.
   - A `PlayingMode` interface: input events in (note on/off, velocity, sustain, mod wheel) → syllable requests out (syllable, start time, shift, gain). Implement `ModeAB`, which holds the per-key `Singer` state machine: first syllable now; next syllables scheduled sample-accurately while the key is held; release cancels anything not yet started.
   - `WhistleVoice`: a phase-accumulating sine on the syllable's contour (log-frequency interpolation between 1 ms points) times the amplitude curve.
     - Optional **bird harmonics**: 2nd at −30 dB, 3rd at −33 dB, on by default (D-022).
     - No breath.
     - Short (2–3 ms) fades at the syllable edges.
3. **Plugin:**
   - A voice path that uses `ModeAB` + `WhistleVoice` per key, through the existing `VoiceAllocator` (polyphony 1/2/4/6, steal with fade, pan spread).
   - A small **room** (convolution or a light algorithmic reverb, about −21 dB wet; on by default, as in the prototype).
   - The existing anatomical path stays in the code, but is **not** the default (see R2).
4. **Parameters** (new IDs need a `proposed` decision entry):
   - voice (whistle / anatomical-experimental);
   - bird harmonics on/off;
   - note rule;
   - mapping (own height / follow);
   - first syllable on the note;
   - snap (also on the mod wheel, CC1);
   - start (random / phrase / shuffled);
   - pause between phrases (0.1–2 s);
   - size (±12 semitones);
   - room;
   - polyphony;
   - volume.

   MIDI: velocity → gain (curve as in the prototype: 0.12 + 0.28·v^1.5), CC64 sustain.
5. **Minimal editor controls** for the parameters above, as plain sliders and combos. The full Perform UI is R4.
6. **Tests and a listening page** (§5).

## 4. Specification details (from the prototype)

- **Mapping.**
  - **Own height:** target = the MIDI note's pitch class at the octave whose cents value is nearest to (bird centre + 100·size).
  - **Follow:** target = (note + 36 + size) × 100 cents.
- **Shift for a syllable with anchor a:**
  - natural = a + (target − centre);
  - shift = (1 − k)·natural + k·target − a, where k = 1 for the first syllable when "first on note" is on, otherwise k = snap.
- **Timing within a phrase:** onset-to-onset = the real `t_ms` differences. At the end of a phrase: last end + pause. In shuffled order: 20–70 ms gaps, with a pause every 4–9 syllables.
- **A behaviour:** if the key is released before the second syllable starts, exactly one syllable plays.
- **Polyphony:** a new note on the same key releases the old singer, which finishes its syllable. Over the polyphony limit, the oldest held singer is released.

## 5. Tests and acceptance criteria

| # | Criterion |
|---|---|
| 1 | **A:** a 30 ms tap produces exactly one syllable, starting ≤ 5 ms after note-on (sample-accurate within the block). |
| 2 | **B:** holding 2 s from a fixed seed plays syllables in the bank's real order from the start index, with real onset gaps (±1 ms). Release mid-syllable → that syllable completes, nothing after. Release during a phrase pause → silence. |
| 3 | **Placement:** the first syllable's rule-anchor lands on the target within ±2 cents. Own height chooses the octave nearest the bird's centre. Follow = note + 36 semitones. |
| 4 | **Voice colour:** on a steady syllable, H2/H3 = −30/−33 dB ± 1 dB (measured with `scripts/review/track.py`). No breath noise. |
| 5 | **No clicks:** low-frequency (< 1.5 kHz) energy around every syllable onset and offset ≤ the real recording + 3 dB. Use the metric in `scripts/studies/engine_gate_poc.py` (`metrics`); the real recording measures about −61 dB at onsets and about −62 dB at offsets. |
| 6 | **Equivalence with the prototype:** for the same bank, seed and MIDI, the plugin's pitch curve matches the prototype algorithm within 5 cents. Port the placement and order logic to a Python reference and compare. |
| 7 | **Real time:** zero allocations in `processBlock` over 60 s of playing (existing harness); CPU for 6 voices reported. |
| 8 | **Birdness gate (listening):** the human rates a plugin render of real phrases (MIDI held notes) against the real recording, using the MUSHRA page `scripts/studies/colour1_page.html` (see `engine1_make.py` for how to build one). The whistle path must reach **≥ 90** on average, as the prototype voice did (98–100). |
| 9 | pluginval strictness 5 (VST3 and AU), auval, and `scripts/check.sh` all pass. |

## 6. Listening page

- A melody tapped on the keyboard, with first syllable on the note: own height vs follow mapping.
- A held note for 6 s, real phrases from a random start.
- A chord of 4 held notes (a small flock).
- The same phrase with snap 0 / 0.5 / 1.
- A plugin render next to the web prototype's (same seed).
- The MUSHRA birdness check (§5 #8).

## 7. Out of scope (later work packages)

- **Variations** (R3; the human: "real syllables first, variations later, within the bird's style").
- **Anatomical engine** fix and re-test (R2, optional).
- **Full Perform UI and presets** (R4).
- **More recordings and species** (later; the bank format must allow it).
- **Second voice / two-voice syllables** (the tracker is partial; the test was inconclusive).

## 8. Pitfalls

- **Audio-thread scheduling:** the prototype uses JS timers. In the plugin, schedule syllables sample-accurately inside `processBlock`, from a lookahead queue. No allocation, no locks.
- **Own-height octave wrap:** a rising scale jumps down an octave where the pitch class crosses the bird's centre ± a tritone. That is intended in own-height mode; it is why "follow" exists.
- **Tracker glitches:** some syllables contain two-voice hops (tracker jumps of more than 100 c in 1 ms). The bank's median filter (5 ms) handles most. Optionally flag or drop syllables with large jumps, and log what you drop.
- **Loudness:** keep the natural amplitude ratios within a phrase (normalise per phrase, not per syllable).
- **Pitch is fuzzy at 3–4 kHz** (PITCH1/PITCH2). Do not over-engineer tuning; the human accepted the current pitch feel "for now".
