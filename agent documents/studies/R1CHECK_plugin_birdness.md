# R1CHECK — Does the plugin's whistle sound like the real bird?

> Origin: WP-R1 acceptance criterion 8 ("birdness gate"). Pre-registered 2026-10-12, before any listening.
> Page: `listening/R1/mushra/index.html` (built by `scripts/studies/r1check_make.py`, design hash in `listening/R1/mushra/design.json`). Analysis: `scripts/studies/r1check_analyze.py <results.json>`.

## 1. Question

The R0 studies rated the whistle with the bird's harmonics at 98–100 against the real recording. That whistle was resynthesised offline from the tracked passage. R1 puts the same voice in the plugin, playing the bank's real phrases from MIDI.

Does the plugin still sound like the bird?

## 2. Design (MUSHRA, same template and recipe as ENGINE1)

- **Passages:** 2 passages of 22938, the same as ENGINE1.
  - P1 = 15.40–17.85 s, the bank's phrase 4 (20 syllables).
  - P3 = 26.95–29.50 s, phrase 7 (14 syllables).
- **Versions** (letters shuffled with seed 20261012; screen order shuffled):

  | Version | What it is |
  |---|---|
  | `ref` | the real recording (hidden reference) |
  | `plugin_natural` | the plugin playing the phrase from MIDI: held key A, order "phrase start", "first syllable on the note" off (the bird's own melody, moved −3 cents), all else default (whistle + bird harmonics, room on) |
  | `plugin_default` | same, all defaults: the first syllable is moved onto the key's note (A7) |
  | `whistle_r0` | the R0 whistle on the tracked passage (the COLOUR2 / ENGINE1 favourite) |
  | `anchor` | sine with strong harmonics (buzzy low anchor) |

- **Matching:** every synthetic version gets the recording's background-noise bed and is loudness-matched on the bird's voiced frames, as in ENGINE1. The plugin versions are time-aligned with the real phrase (envelope cross-correlation: 0 ms lag, r = 0.97 / 0.98).

## 3. Pre-registered decision rule

1. **Listener check:**
   - the hidden reference is rated ≥ 90 on both screens;
   - the anchor is the lowest on both screens.

   If either fails, the run is reported as unreliable and repeated.
2. **Criterion 8 (pass/fail):** the mean of `plugin_natural` over P1 and P3 is **≥ 90**.
3. **Information only** (no decision attached):
   - `plugin_natural − whistle_r0`, per passage and mean. Expected about 0: the same voice, plus the room.
   - `plugin_default − plugin_natural`: what "first syllable on the note" costs in resemblance. This is a design choice the human already accepted (D-020).

## 4. Results

Run 2026-10-12 by the human (Firefox, 48 kHz). About 30 s per screen; each version heard once, the reference twice. Raw data: `agent documents/studies/data/R1CHECK/` (results + design).

| Version | P1 | P3 | Mean |
|---|---|---|---|
| ref (hidden) | 100 | 100 | 100 |
| plugin_natural | 100 | 100 | **100** |
| plugin_default | 100 | 100 | 100 |
| whistle_r0 | 100 | 100 | 100 |
| anchor | 31 | 11 | 21 |

- **Listener check:** passed. The hidden reference was rated ≥ 90 on both screens, and the anchor was the lowest on both.
- **Criterion 8: PASS.** `plugin_natural` scored 100 on average (≥ 90).
- **Information:**
  - `plugin_natural − whistle_r0` = 0 / 0.
  - `plugin_default − plugin_natural` = 0 / 0. At this level of listening, putting the first syllable on the note costs nothing.
- **Reading:** the plugin's whistle playing a real phrase from MIDI is indistinguishable from the real recording for this listener, like the R0 whistle (COLOUR2 / ENGINE1: 98–100).
- **Limit:** this is a ceiling effect. All four non-anchor versions scored 100, so the test cannot rank them.

The human's notes on the listening page (same day):
- "It all sounds great in terms of 'like a bird'."
- Follow mapping (02) "sounds like Frère Jacques more than [own height] by far".
- Snap 0 / 0.5 / 1: "coherent with what I am supposed to hear".
- Plugin vs prototype (08/09): "indistinguishable, almost perfect".
- Real vs plugin (10/11): "they match".
