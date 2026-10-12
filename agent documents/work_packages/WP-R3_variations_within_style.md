# WP-R3 — Variations within the bird's style

| Field | Value |
|---|---|
| Phase | R · Depends on R1 (and R4 if done first) · Size M–L |

**Goal.** Let the bird produce **new** material that still sounds like this finch. The human decided "variation stays within this bird's style" (approach review §10, decision 2).

**Options, safest first** (approach review, "Approach ideas"):
1. Small, data-calibrated perturbations of real syllables: timing ±, transposition ±, scaled to the bird's own spread.
2. Morphs between two real syllables of the same type: time-align (DTW), then blend.
3. A per-type shape model (functional PCA on swipes and peeps). Variation = distance from the type mean, calibrated to the real spread.
4. A sequence model of syllable labels learned from the annotations, replacing the real order. The motif swipe-swipe-swipe-(swipe+swipe)-Lswipe recurs at 11.99 s and 22.00 s.

**Method.** Each option is checked with a short blind listening test against real phrases, as in COLOUR1/2 (MUSHRA page, pre-registered rule). Only options rated close to the real bird stay. Variation is exposed as one control: 0 = the bird's real syllables (R1), 1 = its natural spread.

**Data.** Many more zebra finch recordings are available (human, 2026-10-11). Ingesting them (annotation, tracking, bank export) widens the repertoire and the variation statistics, and may come first. Ask the human where they are.

## Notes from WP-R1

- **Ingesting more recordings.** One command per recording, using its tseg annotation JSON. The recipe is species- and recording-agnostic (D-026):

  ```bash
  PYTHONPATH=python python/.venv/bin/python -m cuicui.bank --wav <rec>.wav --annotations <rec>.json --name <name> --species "<species>" --out assets/voicebanks/<name>/bank.json
  ```

  The plugin currently compiles in one factory bank (`plugin/CMakeLists.txt`, `juce_add_binary_data`). Several banks need a bank parameter, or loading from files, off the audio thread.
- **Tracker hops.** 28 of 123 syllables of 22938 are flagged `hop`: a pitch step of more than 300 cents in 1 ms, probably the tracker jumping between the two voices. They are kept, as in the prototype. Variation work should not learn shapes from those steps (filter with `flags`).
- **Harmonics.** The bank's `stats.measured_h2_db` / `measured_h3_db` give −32.7 / −34.2 dB over all syllables. The voice uses D-022's −30 / −33 dB.
- **Where variation plugs in.** `PlayingMode` emits `SyllableRequest`s.
  - Per-syllable variation (timing, transposition): perturb the requests, or wrap `PhraseOrder`.
  - New shapes need a voice that takes contours rather than bank indices: extend `WhistleVoice::start()` with a contour pointer.

## Notes from WP-R4

**The Variation control.** The Song card (`plugin/Source/ui/PlayPage.cpp`) has room below Pause for a Variation knob or slider:
- add the widget with `setHelp(*w, "perf.variation_or_new_id")`;
- add the sentence to `plugin/Source/ParamHelp.h` (a test fails otherwise);
- a new parameter needs a decision entry (registry contract).

**More birds (banks).**
- The header has room for a bird / bank selector next to the preset bar.
- Presets already carry `voice_bank` (informative in v1, D-028). Once banks exist, a preset can choose its bird.
- Bank switching must stay off the audio thread (`SyllableEngine::setBank` silences voices).

**The display reads `SyllableFeed` events** (syllable index, shift, key, target).
- New syllables made by variation need their contour reachable by the UI. Either keep them in a bank-like store the editor can read, or extend the feed with a contour pointer.
- Without that, the display cannot draw them.

**Recordings.** `recordings/` holds three "House Finch" MP3s (git-ignored, added 2026-10-12). The bank builder (`python -m cuicui.bank`) needs WAV + tseg annotation JSON.
