# VARY2 — How much variation? 100 %, 130 % or 160 % of the bird's own spread

> Study protocol, written **before** data collection (pre-registration, WP-R3, feedback round). Status: **done 2026-10-14, results in §5** (sections 1–4 unchanged since before the test). Nothing kept by the rule; the human moved on to STRETCH1.
> Page: `listening/R3/vary2/index.html` (self-contained) · generator: `scripts/studies/vary2_make.py` (template `scripts/studies/vary2_page.html`) · design: `listening/R3/vary2/design.json` (design hash `8d46398ad3a7`) · analysis: `scripts/studies/vary2_analyze.py <results.json>`.
> Origin: the R3 listening page. The human: "i feel the variations could be a bit increased", and the third syllable of items 03 / 06 sounded too quiet. The cause was the loudness variation: a rare draw, a loudness spread inflated by the per-phrase normalisation, and a pull toward the type's average. The human chose "1b 2a": no loudness variation, then this blind test.

## 1. Question

With the plugin's shape model as it is now, does going beyond the bird's own spread still sound like the bird singing its song again? The shape model now has no loudness variation and caps every draw at ±2 sd. VARY1 tested 100 % (rated like the bird's own renditions) and 200 % (clearly less like the bird); this test asks about 130 % and 160 %.

## 2. Material

The same two "same song again" passages as VARY1 part A:
- **A1:** 22937, 16.15–27.25 s, one song sung 3 times;
- **A2:** 22938, 63.80–73.17 s, two renditions of one song.

The banks are `hf_22937` and `hf_22938` (INGEST1, with their shape models).

| Version | What it is |
|---|---|
| `real` | the whistle on the bird's own renditions (its real variation) |
| `same` | the first song's rendition, sung again identically at each song's place (Variation 0) |
| `v100` | that rendition, every time a new rendition at 100 % (the plugin's `control::makeVariant`, via `cuicui_core.variant`) |
| `v130` | the same, 130 % |
| `v160` | the same, 160 % |
| `anchor` | `real` with strong harmonics |
| `ref` | the hidden reference |

- Each syllable occurrence has its own seed. The draws are the same for v100, v130 and v160, so the three versions differ only in how far they go.
- The voice, background bed, loudness matching and page are as in VARY1.

## 3. Method

- **Listener:** the human, headphones. MUSHRA-style: the real recording (Reference) and the versions in random order, with the reference also hidden among them.
- **Rating** 0–100: "how much does it sound like this bird singing its song again?" 100 = as natural as the recording.
- **Seed** 20261015. Two screens, in random order.

## 4. How the results are used (fixed before data)

1. **Means** over the two screens, per version.
2. **Listener check:** the hidden reference is rated ≥ 90 on each screen. If not, it is reported (and still counted).
3. **A level keeps the bird** if its mean is **at least `real` − 5**.
4. **The Variation control:**
   - L = the largest of 100 / 130 / 160 % such that L and every lower tested level keep the bird;
   - the parameter `perf.natural_variation` then goes from 0 to L/100, with **default L/100**;
   - 1.0 still means the bird's own spread;
   - if 100 % itself does not keep the bird, nothing changes (default 1.0) and the human is asked.
5. **Exploratory (no decision):**
   - `same` vs `real` (VARY1: 25 points lower);
   - `anchor` at least 20 below `real` (the test discriminates);
   - a level that keeps the bird above one that does not (non-monotone) is reported.

## 5. Results

Listener: the human, Firefox, 2026-10-14 (2 minutes). Raw data: `studies/data/VARY2/`.

| Version | A2 (22938) | A1 (22937) | mean |
|---|---|---|---|
| hidden reference | 97 | 96 | 96.5 |
| real (the bird's own renditions) | 97 | 64 | 80.5 |
| v130 | 84 | 67 | 75.5 |
| v160 | 83 | 67 | 75.0 |
| same (identical repetition) | 83 | 63 | 73.0 |
| v100 | 84 | 58 | 71.0 |
| anchor | 11 | 6 | 8.5 |

- **Rule:** the bar is real − 5 = 75.5. v100 does not keep the bird (71.0), so nothing changes (default 1.0) and the human is asked. v130 sits exactly on the bar; v160 is below it.
- **The listener:** "i cannot tell the difference between the synthesising versions. did you add variation?"
- **Reading:**
  - The plugin's shape model, now without loudness variation and with draws capped at ±2 sd, is **not audible**: identical repetition and 100 / 130 / 160 % all sit within 4.5 points.
  - On A1 even the bird's own renditions scored like the repetition (64 vs 63).
  - In VARY1 the audible difference between `same` and the shape model was therefore most likely the **loudness variation** (and the uncapped note draws), not the shape.
- **Next (the human):** "variations are not critical. a little bit would be enough." Try only syllable length (80–130 %) and pitch range around the note (80–130 %), and hear them on a dozen syllables: STRETCH1 (`listening/R3/stretch1/`).
