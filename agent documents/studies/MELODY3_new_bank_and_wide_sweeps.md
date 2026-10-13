# MELODY3 — Does the new bank play Frère Jacques, and do very wide sweeps blur it?

> Study protocol, written **before** data collection (pre-registration, WP-R5). Status: **done 2026-10-13, results in §5** (sections 1–4 unchanged since before the test).
> Page: `listening/R5/melody3/index.html` (self-contained) · generator: `scripts/studies/melody3_make.py` (template `scripts/studies/melody3_page.html`, from MELODY2's) · hidden key: `listening/R5/melody3/design.json` (design hash `97f6e1fab5b0`) · analysis: `scripts/studies/melody3_analyze.py <results.json>`.
> Origin: the human chose rule E (MELODY2) and kept the 11 notes set by ear (TUNE1); the bank is rebuilt (`assets/voicebanks/zf_22938/bank.json`, hash `9e0e08577e5a`). TUNE1's exploratory reading: the remaining Frère Jacques problem is not the note values. Screens with more **very wide sweeps** rated lower (r = −0.79). The human chose to test that (answer "2a").

## 1. Questions

1. **WP-R5 acceptance criterion 3:** tapped on the new bank, is Frère Jacques recognisable on 5 different draws of syllables, without its ending going up?
2. **Very wide sweeps:** do tunes get clearer when a tap never starts with a very wide sweep?

## 2. Method

- **Listener:** the human, Firefox, headphones. Blind: versions shuffled and lettered A–D per screen; screens shuffled.
- **Material:** Frère Jacques, first two lines (R1's 14 notes C4–G4 and R1's timing). Played as in MELODY2:
  - 60 ms taps, one syllable per key;
  - follow the keyboard, Snap to note 0, first syllable on the note;
  - bird harmonics and room on;
  - centred (stereo spread 0), velocity 100.

  Rendered by the core whistle path (`cuicui_core.sing`).
- **Screens:** 5 seeds, **501–505** (fixed before rendering; new seeds, not MELODY2's).
- **Versions** (one per screen each):

  | Version | Bank | What changes |
  |---|---|---|
  | `new` | the rebuilt bank: rule E + the 11 notes set by ear | — (the candidate default) |
  | `no_wide` | the same, with the **very wide sweeps** flagged `no_tap` | a tap that would start with one draws again (D-032 mechanism); every other note is the same syllable as `new` |
  | `old` | the R1 bank (rule A, hash `09b16ee69ac8`) | the same syllables as `new`, placed by the old rule |
  | `anchor` | the new bank, each syllable's note moved a random 4–6 semitones up or down | deliberately out of tune |

  - **Very wide sweep:** a playable syllable whose loud part (frames within 12 dB of its peak) spans **more than 8 semitones**: 13 of the 118 (#13, 17, 80, 81, 83, 92, 93, 95, 102, 106, 113, 114, 121).
  - **Design facts** (from the generator): `new` draws 3, 4, 0, 3 and 1 very wide sweeps on seeds 501–505. On seed 503, `no_wide` is therefore identical to `new`: the listener's two ratings of the same sound measure the rating noise.
- **Each screen:**
  - the reference button: the tune on a soft tone at the bird's target pitches (as MELODY2);
  - per version, a slider: **"How well does the bird play this tune?"**, from 0 (not the tune) to 100 (clearly the tune);
  - per version, two ticks:
    - **"I would recognise the tune without being told"** (the human's own test in MELODY1);
    - **"The ending goes up"** (MELODY1: "the ending goes up in the notes when normally it is supposed to repeat the last 3 notes").
- About 5–8 minutes.

## 3. Decision rules (fixed before data)

1. **Listener check:** the anchor is rated lowest (ties count) on **at least 4 of 5** screens. Otherwise the run is unreliable and repeated.
2. **Very wide sweeps:** consider only the screens where `no_wide` differs from `new` (4 screens: 501, 502, 504, 505).
   - `no_wide` is **adopted** if, on those screens, its mean rating beats `new`'s by **≥ 10 points** and it is rated higher on **at least 3 of the 4**.
   - Adopted means:
     - the bank builder flags very wide sweeps `no_tap` (rule above, in `cuicui.bank`, recorded in a decision entry);
     - taps and first syllables avoid them;
     - they still sing inside phrases.
   - Otherwise it is **not adopted**: no flag, a backlog line.
3. **Criterion 3 (WP-R5)** is judged on the version that becomes the default (`no_wide` if adopted, else `new`). It **passes** if that version is ticked "I would recognise the tune without being told" on **5 of 5** screens and "the ending goes up" on **0 of 5**.
4. **Reported, no decision attached:**
   - `new − old` per screen and on average (what rule E + the ear's notes bring);
   - all ticks per version;
   - on seed 503, the difference between the two ratings of the identical sounds;
   - which syllables `no_wide` replaced.

## 4. Limitations

- One listener, one tune, five draws. Frère Jacques's notes are all within C4–G4 (the bird sings C7–G7).
- `no_wide` changes which syllables are heard, so a difference mixes "no wide sweep" with "another syllable". The redraw keeps every other note identical, which limits this to the replaced notes.
- The ticks are yes/no answers on a small sample: criterion 3 is a pass/fail check, not a measurement.

## 5. Results (2026-10-13, one session, 5 screens, 4 min, Firefox)

Data: `studies/data/MELODY3/` (results + design) · analysis: `scripts/studies/melody3_analyze.py` → `listening/R5/melody3/analysis/melody3_report.json`.

| Version | 501 | 502 | 503 | 504 | 505 | **Mean** | recognised | ending goes up |
|---|---|---|---|---|---|---|---|---|
| new (rule E + ear notes) | 63 | 61 | 61 | 71 | 80 | **67.2** | 2 (502, 505) | 1 (501) |
| no_wide | 64 | 79 | 60 | 71 | 80 | **70.8** | 2 (502, 505) | 1 (501) |
| old (R1, rule A) | 53 | 46 | 53 | 36 | 59 | **49.4** | 0 | 0 |
| anchor | 36 | 7 | 31 | 10 | 7 | 18.2 | 0 | 0 |

The listener's note: "first page i did not see the checkboxes and i haven't filled them." The first screen was seed 504, so its ticks are missing, not "no".

### Pre-registered analysis
1. **Listener check: PASS.** The anchor was the lowest on 5 of 5 screens.
2. **Very wide sweeps: not adopted.** On the 4 screens where the versions differ, `no_wide − new` = +1 (501), +18 (502), 0 (504), 0 (505). The mean is +4.8 (< 10), and `no_wide` is higher on 2 of 4 (< 3). No flag goes into the bank; a backlog line.
3. **Criterion 3: FAIL** (judged on `new`): recognised on 2 of 5 screens and "the ending goes up" on 1. Counting only the 4 screens with answered ticks, it is 2 of 4 and 1 of 4: still a fail.
4. **Information:**
   - `new − old` = +10, +15, +8, +35, +21: **+17.8 on average, higher on every screen.** Rule E with the ear's notes clearly beats the R1 bank.
   - Seed 503, the same sound heard twice: 61 and 60 (1 point apart). The ratings are consistent.
   - `no_wide` replaced 3, 4, 3 and 1 notes. Its one clear gain (502, +18) replaced 4 syllables, two of them #92, the widest sweep (11 semitones).

### Reading (exploratory)
- The notes are right, but a tapped tune is still recognised only on some draws of syllables.
- Each tap draws an unrelated shape. One note starts 4 semitones below and stays low, the next starts above and falls 5. Each sits on its note alone (TUNE1), but the steps between neighbours get lost.
- The best screen (505, 80) used mostly narrow syllables. Across the 14 screens of MELODY2 and MELODY3, though, a screen's mean sweep width goes only weakly with its rating (r = −0.41). Width alone is not the explanation.
