# WP-R5 — Melodies that sound right: syllable notes and held keys

| Field | Value |
|---|---|
| Phase | R · Depends on R4 · comes **before R3** (D-031) |
| Size | M (one session; the human is needed for one ~15-minute listening test and one choice) |
| Milestone | M4' — you can play a tune on the bird |

## 1. Goal

A tune played on Syrinx must be recognisable, as R1's render of Frère Jacques was, without losing the bird.

MELODY1 (`agent documents/studies/MELODY1_tapped_melody.md`) found two causes:
1. **The note of a falling glide is set at its quiet bottom.**
   - The default note rule (D-019, "longest steady part, else the ending") takes the plain mean of the last quarter of the curve when a syllable has no steady part (37 % of the playable syllables).
   - For glides, that is the quiet bottom. The ear hears the loud part, often 3–6 semitones higher.
   - So the bird shifts these syllables up, and a tune's notes come out too high. The human heard this at the end of Frère Jacques.
2. **Held keys add the bird's own melody between your notes.**
   - A tap gives one syllable, on the note. A held key gives 3–4: the following ones are at the bird's own pitches unless Snap to note is 100 %.

Fix 1 with a note rule chosen **by ear** (a pre-registered blind test, MELODY2). Then let the human choose how held keys and the release call (D-030) behave in melodies, from renders.

## 2. Read first

- `agent documents/studies/MELODY1_tapped_melody.md` (all), and the results of PITCH1 and PITCH2 (where D-019 came from).
- `decisions.md`: D-019, D-020, D-021, D-026, D-030, D-031.
- Code:
  - `python/cuicui/bank/build.py` (`anchors()`: how notes are computed);
  - `core/include/cuicui/control/placement.hpp`, `mode_ab.hpp`, `phrase_order.hpp`;
  - `python/bindings/module.cpp` (`cuicui_core.sing(bank_path, …)` renders with any bank file).
- Reports: R4 §12 (handoff) and R1 §12 (bank, reference, determinism).
- The rating-page pattern: `scripts/studies/colour1_page.html` + `colour1_make.py` / `colour1_analyze.py`.
- The MELODY1 scripts: `scripts/studies/melody1_cases.py`, `melody1_reference.py`.

## 3. Deliverables

1. **Candidate note rules** in `cuicui.bank` (Python), with unit tests on synthetic contours (steady, rising glide, falling glide, glide with a quiet tail).
   - All candidates first drop the frames more than 20 dB below the syllable's loudest frame. The tracker is unreliable there: #95's last 6 ms read 1.76 kHz, which is not in the recording.
   - Candidates (the letters are for the study; the page shows them shuffled):
     - **A:** today's rule (D-019), unchanged. The control.
     - **B:** longest steady part, else the **ending of the loud part**: the last quarter of the frames within 10 dB of the peak, loudness-weighted.
     - **C:** longest steady part, else the **loudness-weighted mean** of the syllable.
     - **D:** "steady parts" (stability-weighted mean; already in the bank).
     - **E:** loudness-weighted mean for every syllable.
2. **Study MELODY2** (pre-registered, §4.2): `agent documents/studies/MELODY2_*.md`, `scripts/studies/melody2_make.py` / `melody2_analyze.py`, the page in `listening/R5/melody2/`.
3. **The chosen rule as the default:**
   - The bank `assets/voicebanks/zf_22938/bank.json` is rebuilt: same syllables, same order, new anchor values, new hash.
   - The four Melody-card choices stay. If the winner is not one of them, it replaces "longest steady part, else ending" as the default ("rule") and the card's label is reworded. A fifth anchor needs a decision entry.
4. **Held keys in melodies**, decided by the human from renders (§4.3), then implemented with tests.
5. Tests, listening page, report, state, decisions (D-031 → accepted, plus the held-key decision).

## 4. Specification

### 4.1 Measuring before asking

For each candidate, report the proxy: the share of playable syllables whose note is within ±2 semitones of their loudness-weighted pitch.
- Today, with rule A: 75 %. "Steady parts": 92 %. "Average": 87 %.
- The proxy favours loudness-weighted rules by construction, so it informs, the ear decides.

### 4.2 MELODY2 — which note rule makes tunes recognisable? (pre-register before rendering the page)

**Material:**
- Three melodies on keys C4–C5: Frère Jacques (R1's 14 notes and timing); "Au clair de la lune" (C C C D E D C E D D C); a C major scale up and down.
- Each melody is played as 60 ms taps, 0.42 s apart, with "follow the keyboard", Snap to note 0, first syllable on the note, and harmonics and room on.
- **Three seeds per melody**, so the screens hear different syllables: 9 screens.
- Render with `cuicui_core.sing(<candidate bank>, …)`, one bank file per candidate. **Same seed, same playable set → the same syllables**: only the placement differs.

**Each screen:**
- A **reference button**: the melody on a soft tone at the bird's target pitches (as MELODY1 item 20).
- Versions **A–E**, plus an **anchor**: rule A with each syllable shifted by a random ±4–6 semitones.
- The question: "How well does the bird play this tune?" from 0 (not the tune) to 100 (clearly the tune).

**Decision rule** (write it in the study doc before the human listens):
1. **Listener check:** the anchor is rated lowest on at least 7 of 9 screens; otherwise rerun.
2. The candidate with the highest mean becomes the default if it beats A by **≥ 10 points**.
3. If several candidates are within 5 points of the best, prefer, in this order:
   - one already in the bank (D, then E);
   - otherwise the simplest (C before B).
4. If none beats A by 10, keep A, report it, and ask the human.

### 4.3 Held keys — the human chooses from renders

After the rule is chosen, render the same three melodies played as a person plays them: notes held 0.35 s, and legato.

The options:
- **(a)** The bird continues its phrase only after a hold threshold (try 0.4 and 0.5 s). Below it, a key gives one syllable.
- **(b)** No release call (D-030) when another key starts within a short time after the release (legato; try 150 ms). The call then ends a line, not every long note.
- **(c)** A factory preset "Finch, melody": follow the keyboard, Snap to note 100 %, Birds 1. In MELODY1, Snap 100 % made held-key melodies recognisable (items 05 and 06).
- **(d)** Nothing: today's behaviour.

Show the renders, A/B against today's behaviour, and let the human pick, possibly several. Implement only what was picked: (a) and (b) in `ModeAB`, `PlayContext` defaults, tests; (c) as a preset file.

## 5. Suggested steps

1. Rebuild the MELODY1 page with its two scripts; read the study.
2. Implement candidates A–E in `cuicui.bank`, with unit tests. Build one candidate bank per rule in `listening/R5/work/`, not in `assets/`. Report the proxy (§4.1).
3. Write the MELODY2 study doc, with the design, a design hash and the decision rule. Then build the page and open it for the human, and **wait** for the results.
4. Analyse exactly as pre-registered. Record the results and the decision (D-031).
5. Rebuild `assets/voicebanks/zf_22938/bank.json` with the chosen default:
   - run `python -m cuicui.bank` with the R1 recipe plus the new rule;
   - re-embed the bank in the PROTO1 page (`scripts/studies/proto1_make.py`, which uses the committed bank);
   - run all tests.
6. Render the held-key options (§4.3). The human picks; implement and test.
7. Build the listening page `listening/R5/`, run `scripts/check.sh` and `scripts/validate_plugin.sh 5 --gui`, write the report.

## 6. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | MELODY2 run by the human and analysed exactly as pre-registered; the decision recorded (D-031 accepted) |
| 2 | With the default rule: ≥ 90 % of playable syllables within ±2 semitones of their loudness-weighted pitch (today 75 %), or the human's ear overruled the proxy (recorded) |
| 3 | Tapped Frère Jacques (follow, 60 ms taps) with 5 seeds: the human recognises the tune in all 5, and its end no longer goes up |
| 4 | Bank rebuilt and committed. Same syllables in the same order: the syllable count, order and labels are checked against the old bank. The R1 / R4 tests pass; the reference comparisons still run with D-030 off. |
| 5 | The held-key choice (§4.3) implemented, with tests for each part picked. Examples: (a) a key held under the threshold gives exactly one syllable; (b) a release followed within the window by another key gives no call |
| 6 | Birdness unchanged: the "natural" phrase render (R1CHECK `plugin_natural` setup) differs only in the first syllable's shift |
| 7 | `scripts/check.sh` and `scripts/validate_plugin.sh 5 --gui` pass |

## 7. Listening & results

`listening/R5/`:
- the MELODY2 page (blind);
- the three tapped melodies before and after the new rule (A/B), with the reference melody;
- the held-key options;
- a natural phrase, before and after, to check that the bird itself did not change.

## 8. Out of scope

- Variations (R3).
- More recordings (R3).
- Two-voice tracking (backlog).
- The web prototype's playing logic (it gets the new bank only).
- New syllable selection, such as "prefer clear syllables for taps": backlog, unless MELODY2 fails.

## 9. Pitfalls

- **The ear decides, not the proxy.** The loudness-weighted proxy favours rules C and E by construction.
- **Compare rules on the same syllables.** The same seed draws different syllables when the playable set changes (D-030 already did this once). Hold the bank's syllables and order fixed; change only the anchor values.
- **Do not trim the bank's contours.** The quiet tail is part of the sound. Only the note computation ignores frames below −20 dB.
- **Keep the bank format v1 (D-026).** Change values, not the structure, unless a fifth anchor is needed (then a decision entry).
- **The anatomical voice and the mode-C/D/E plans are untouched.**
