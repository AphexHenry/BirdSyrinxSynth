# MELODY2 — Which note rule makes tunes recognisable?

> Study protocol, written **before** data collection (pre-registration, WP-R5 §4.2). Status: **done 2026-10-13, results in §7** (sections 1–6 unchanged since before the test).
> Page: `listening/R5/melody2/index.html` (self-contained) · generator: `scripts/studies/melody2_make.py` (candidate banks from `scripts/studies/melody2_banks.py`; page template `scripts/studies/melody2_page.html`) · hidden key: `listening/R5/melody2/design.json` (design hash `9cd218cde550`) · analysis: `scripts/studies/melody2_analyze.py <results.json>`.
> Origin: MELODY1. With the default note rule (D-019), a falling glide's note is its quiet bottom, and tapped tunes come out too high at those notes ("the ending goes up").

## 1. Question

When the bird plays a tune with one syllable per key, each syllable placed so that its **note** lands on the key, which definition of a syllable's note makes the tune most recognisable?

## 2. Candidates

Every candidate gives each syllable one note (cents). Only that value differs between versions: the bank's syllables, their order and their curves are the same (`listening/R5/work/bank_<X>.json`, built with `cuicui.bank`, `BuildSettings(note_rule=X)`).

| Candidate | A syllable's note |
|---|---|
| **A** (control) | today's rule (D-019), unchanged: the longest steady part (±50 c, ≥ 20 ms), else the ending (plain mean of the last quarter of the voiced curve) |
| **B** | longest steady part, else the **ending of the loud part**: the last quarter of the frames within 10 dB of the peak, loudness-weighted |
| **C** | longest steady part, else the **loudness-weighted mean** |
| **D** | "steady parts": the stability-weighted mean, weight exp(−\|dP/dt\| / 2000 c/s) |
| **E** | the loudness-weighted mean, for every syllable |

- B–E first drop the frames more than **20 dB below** the syllable's loudest frame. The tracker is unreliable there (MELODY1: #95's last ms read 1.76 kHz, which is not in the recording). A keeps every voiced frame: it is today's sound.
- "Loudness-weighted" = weighted by amplitude² (energy), as PITCH1's "energy" rule and MELODY1's measurements.
- The steady part of B and C is searched on the kept frames.

## 3. Method

- **Listener:** the human (musician, n = 1), Firefox, headphones.
- **Material:** three melodies on the keys C4–C5, played with "follow the keyboard" (the bird sings 3 octaves above the key):
  - **Frère Jacques**, first two lines: C D E C, C D E C, E F G, E F G. R1's 14 notes and R1's timing (taps 0.42 s apart, longer before the 2nd line and the last group).
  - **Au clair de la lune**: C C C D E D C E D D C, on a 0.42 s beat with the song's own rhythm (E and the next D last two beats; the final C is the end).
  - **C major scale** up and down: C D E F G A B C B A G F E D C, 0.42 s apart.
- **Playing:** every note is a 60 ms tap (one syllable per key, the first syllable lands on the note). Settings: follow the keyboard, Snap to note 0, first syllable on the note, bird harmonics on, room on, release call rule on (taps never call), all else default. Rendered by the core whistle path, `cuicui_core.sing(<candidate bank>, …)`.
- **Three seeds per melody**, so the screens hear different syllables: **9 screens**. Same seed, same playable set → the same syllables in every version of a screen (checked by the generator). Only the placement differs.
- **Each screen:**
  - a **reference button**: the melody on a soft tone at the bird's target pitches (as MELODY1 item 20), at about the bird's loudness;
  - **six versions**, shuffled on every screen and labelled A–F (page letters are unrelated to the candidate letters): candidates A–E, plus an **anchor** (candidate A with each syllable moved a random 4–6 semitones up or down; one random offset per syllable, seed fixed);
  - the question: **"How well does the bird play this tune?"**, a slider from 0 (not the tune) to 100 (clearly the tune).
  - Every version has the same length and the same gain, so the page can switch between them at the same point while playing.
- **Order:** screens shuffled (seeded); versions shuffled per screen (seeded). About 15 minutes; progress is saved in the browser.

## 4. Decision rule (fixed before data)

1. **Listener check:** the anchor is rated lowest (ties count as lowest) on **at least 7 of 9 screens**. Otherwise the run is reported as unreliable and repeated.
2. **Mean** = each candidate's mean rating over the 9 screens. The candidate with the highest mean becomes the default note rule **if it beats A by ≥ 10 points**.
3. **Near-ties:** if several candidates beat A by ≥ 10 and are within 5 points of the best mean, prefer, in this order:
   - one already in the bank: **D** (it is today's "steady parts" choice), then **E**;
   - otherwise the simplest: **C** before **B**.
4. **No winner:** if no candidate beats A by 10 points, A stays, this is reported, and the human is asked.
5. **Reported, no decision attached:** the mean per melody; per candidate, the number of screens where it beats A; the screen-level spread; the proxy (§6).

## 5. Limitations

- One listener, who has heard MELODY1. The labels are hidden and shuffled, the screens are shuffled.
- Only tapped tunes (one syllable per key, Snap to note 0). Held keys are decided separately (WP-R5 §4.3).
- The bird sings 3 octaves above the keys (C7–C8, about 2.1–4.2 kHz), where a note is fuzzy (PITCH1: ±1.5 semitones on steady tones).
- One bird (22938).

## 6. Measurement made before the test (read **after** doing the test)

<details><summary>The proxy (WP-R5 §4.1)</summary>

Share of the 118 playable syllables whose note is within ±2 semitones of their loudness-weighted pitch (amplitude² over the bank contour), from `listening/R5/work/proxy.json`. The proxy favours loudness-weighted rules by construction: E is the proxy itself, apart from the −20 dB floor.

| Candidate | within ±2 st | beyond ±4 st | median offset (c) | mean \|offset\| (c) |
|---|---|---|---|---|
| A (today) | 74.6 % | 9.3 % | −16 | 154 |
| B | 83.1 % | 1.7 % | +13 | 121 |
| C | 91.5 % | 1.7 % | +1 | 69 |
| D | 96.6 % | 0.0 % | −10 | 58 |
| E | 100.0 % | 0.0 % | 0 | 0.5 |

</details>

## 7. Results (2026-10-13, one session, 9 screens, 9.5 min, Firefox)

Data: `studies/data/MELODY2/` (results + design) · analysis: `scripts/studies/melody2_analyze.py` → `listening/R5/melody2/analysis/melody2_report.json`.

![Ratings per version](assets/melody2_ratings.png)

| Version | AC-1 | AC-2 | AC-3 | FJ-1 | FJ-2 | FJ-3 | SC-1 | SC-2 | SC-3 | **Mean** | SD |
|---|---|---|---|---|---|---|---|---|---|---|---|
| A (today) | 70 | 95 | 92 | 83 | **27** | 84 | 67 | 97 | 94 | **78.8** | 22.2 |
| B | 77 | 95 | 92 | 94 | 72 | 83 | 94 | 96 | 92 | **88.3** | 8.8 |
| C | 77 | 95 | 92 | 74 | 71 | 95 | 93 | 96 | 94 | **87.4** | 10.3 |
| D | 65 | 89 | 92 | 94 | 59 | 95 | 91 | 96 | 66 | **83.0** | 15.0 |
| E | 71 | 95 | 92 | 95 | 71 | 83 | 93 | 96 | 92 | **87.6** | 10.1 |
| anchor | 14 | 5 | 24 | 12 | 6 | 14 | 25 | 7 | 25 | 14.7 | 8.2 |

(AC = Au clair de la lune, FJ = Frère Jacques, SC = scale.)

### Pre-registered analysis
1. **Listener check: PASS.** The anchor was the lowest on 9 of 9 screens.
2. **Winner: none.** The best mean is B (88.3), which beats A by **9.5 points**, just under the 10-point threshold. C (+8.6) and E (+8.8) are within 1 point of B; D is +4.2.
3. **Near-ties:** not reached (no candidate beats A by 10).
4. **Decision rule 4 applies:** A stays by the rule; the result is reported and **the human is asked** (WP-R5 report §0).
5. **Information:**
   - Mean per melody (A / B / C / D / E): Au clair 85.7 / 88.0 / 88.0 / 82.0 / 86.0; Frère Jacques **64.7** / 83.0 / 80.0 / 82.7 / 83.0; scale 86.0 / 94.0 / 94.3 / 84.3 / 93.7.
   - Screens where a candidate beats A: 4 of 9 for each of B, C, D, E; below A on 3 / 2 / 4 / 3. Most screens sit near the ceiling (92–97 for every candidate), so A's lower mean comes from two screens: FJ-2 (27 against 59–72) and SC-1 (67 against 91–94).
   - A has the largest spread (SD 22); B, C and E the smallest (9–10).
   - Proxy (§6): A 74.6 %, B 83.1 %, C 91.5 %, D 96.6 %, E 100 %.

### The listener's comment
"Frère Jacques still has this last note that is falsely tuned for some reason." In the chat: "the Frère Jacques still has some bird notes that are not tuned well. they play too high, got analysed too low."

### Exploratory (not pre-registered)
- With E, every note of the three Frère Jacques screens has its loudness-weighted pitch exactly on the target, yet notes still sound too high. So for some syllables the ear hears more than the loud average.
- The suspects: syllables whose loud part (within 12 dB of the peak) reaches far **above** their loudness-weighted note. 19 of the 118 playable syllables reach more than 3 semitones above it; 6 reach more than 4 (#20, #71, #81, #86, #95, #108).
  - Example, FJ-3's last G (#71): in 95 ms it dips 5 semitones, then jumps about 10 semitones up in 10 ms at −8 to −12 dB, and settles back at its loudest part. Placed by its loud average, the jump sits 4–6 semitones above the note.
- Across the 9 screens, the share of notes played by such syllables goes with lower ratings (mean of B, C and E; r = −0.68). FJ-2 has 5 such notes of 14 and rates lowest (71). AC-2 and AC-3 have none and rate highest (95, 92).
- Wide sweeps as such are not the problem: 93 of 118 syllables cover more than 4 semitones in their loud part.
