# INGEST1 — Is it the bird? Checking the automatically marked syllables

> Study protocol, written **before** data collection (pre-registration, WP-R3). Status: **done 2026-10-13, results in §5** (sections 1–4 unchanged since before the test).
> Page: `listening/R3/ingest1/index.html` (self-contained: the recording's audio and spectrograms are inlined, and the whistle is synthesised in the page) · material: `scripts/studies/r3_material.py` · generator: `scripts/studies/ingest1_make.py` (template `scripts/studies/ingest1_page.html`) · design: `listening/R3/ingest1/design.json` (design hash `f6be5b90c077`) · analysis: `scripts/studies/ingest1_analyze.py <results.json>`.
> Origin: WP-R3 scoping. The human: add `22937 - House Finch.wav` (cleaned by the human to 108 s, 2026-10-13), 22938 is a house finch, and "within the style" means the species. The agent offered: "I mark the syllables automatically and you check them on a by-ear page."

## 1. Question

Which of the automatically marked syllables are the bird, with a whistle copy that sounds like it, and which should stay out of the banks? Which of them are the bird's long rising calls ("alarm")?

## 2. Material

- **22938, from 53.3 s to the end (134 s).** The human annotation (and the factory bank) stops at 53.3 s; the rest was one SILENCE segment although the bird sings on. Same bird as the factory bank.
- **22937, the whole cleaned recording** (108 s; Macaulay 22937, house finch).
- **The marker** (`cuicui.bank.segment`, new in WP-R3):
  - a 1 ms frame is tonal when its strongest peak in 1.2–9 kHz is within 40 dB of the recording's loud level and 18 dB above the band median;
  - runs are joined across gaps under 8 ms, cut where the bank's pitch tracker hears a gap of 12 ms or more, trimmed to its voiced frames;
  - pieces under 40 ms are dropped;
  - blocks of syllables (gaps under 500 ms) whose loudest frame is more than 20 dB below the loud level are dropped as far-away birds (the human: "phrases are long and clear blocks separated by 500ms or more. background noises are pretty low").
- **Marker check against the human:** on 22938's annotated part (7.6–53.3 s), all 123 annotated syllables are found, each by one marked syllable. Three marked syllables span two annotated ones, and 4 marked sounds are not annotated.
- **Contours** come from the bank builder, unchanged (`python -m cuicui.bank`, rule E notes). Recordings not at 48 kHz are resampled to 48 kHz first, so that the tracker's 1 ms hop is exact (22937 is 44.1 kHz; 22938 is unchanged).
- **Phrases:**
  - 22938: the builder's 150 ms gap, as in the factory bank;
  - 22937: 500 ms (the human's description).
- **In total:** 674 syllables in 47 phrases (122 s of song), on 16 screens.
  - 22938 rest: 313 syllables, 24 phrases.
  - 22937: 361 syllables, 23 phrases.

## 3. Method

- **Listener:** the human, headphones. Not blind: it is a checking task.
- **Per phrase:**
  - the spectrogram (1–8 kHz) with the tracked pitch curve on top and a proposed label under each syllable;
  - ▶ Real plays the recording (high-passed at 800 Hz);
  - ▶ Whistle plays the copy (the PROTO1 / plugin voice: sine plus the bird's 2nd/3rd harmonics, light room);
  - ▶ Both plays one after the other.
- **Per syllable** (click it): ▶ real, ▶ whistle, a label menu, and the marks **Keep** (default), **Not the bird** (click, noise, another bird) and **Whistle wrong** (the bird, but the copy does not sound like it).
- **Proposed labels:**
  - the majority label of the 3 nearest human-labelled 22938 syllables (median |Δ cents| over 24 points + 600 c per e-fold of length). Leave-one-out on the 123 labelled syllables, this is right 65 % of the time; most errors are swipe ↔ peep;
  - a sound of 200 ms or more that rises by 400 c or more (last 20 ms vs first 20 ms) is proposed as "alarm";
  - an "alarm" proposal that is short or flat becomes "other".
- **Check round:** 10 syllables come back with their earlier mark hidden: up to 5 that the listener marked, the rest from the kept ones. The listener marks them again.
- **Saved:** marks, label changes, plays per button, time per screen. Progress is kept in the browser.

## 4. How the results are used (fixed before data)

1. **A syllable enters its bank unless** it is marked "not the bird" or "whistle wrong" (first pass).
2. **Labels:** the listener's label where changed, otherwise the proposal.
   - Labels starting with "alarm" become release calls (D-030): they end a long key press and never sing inside phrases.
   - Every other label is informative: the variation work uses data-driven types and measured shapes, not these labels.
3. **Reliability (check round):**
   - the check-round mark (keep / reject, where "not the bird" and "whistle wrong" both count as reject) agrees with the first pass on **at least 8 of 10**: the first-pass marks are used as they are;
   - otherwise, the agent reports which ones changed and asks the human before building the banks.
4. **Output:**
   - final annotation files (the marked segments, with the kept syllables' labels; rejected syllables become NOISE segments);
   - one bank per recording, built with `python -m cuicui.bank`;
   - 22938: the human annotation's 145 segments are kept unchanged (indices, labels), so the TUNE1 note corrections still apply.

   How the banks reach the plugin (one bank per bird, a merged bank, or both) is a separate decision for the human, after the listener has heard the new material.

## 5. Results

Listener: the human, Firefox, 2026-10-13 (12 minutes on the screens + 1 minute check round). Raw data: `studies/data/INGEST1/`.

- **Marks:** 3 of 674 syllables marked "whistle wrong", all in 22937. They are the long final notes of songs at 77.6, 81.3 and 86.3 s. None was marked "not the bird", and no label was changed.
- **Check round:** 9 of 10 agree (keep vs reject). 22937:115 was kept first and marked "whistle wrong" in the check round. Rule 3: ≥ 8 → **the first-pass marks are used**.
- **Final banks** (`scripts/studies/r3_banks.py`):
  - **`hf_22938`** (hash `5bc449a1d4c4`): 436 syllables in 36 phrases. Its first 12 phrases equal the factory bank `zf_22938`.
  - **`hf_22937`** (hash `39c926b8b0cb`): 358 syllables in 23 phrases.
  - The annotations are in `assets/voicebanks/hf_*/annotation.json`; rejected syllables are NOISE segments marked `ingest1`.
- **The listener's comment** (verbatim): "the last sound of many phrases, bit longer, tagged as "alarm", is often badly reproduced. something is missing in its analysis. i suspec another mechanism of production of the sound, or that it requires a thinner analysis. the orginal sound in those case feels richer, denser, to have more low frequencies. this has to be investigated at some point in the plan, and the sounds i have discarded during this test could be re-integrated once we find a better way to render them."
  - Recorded as a known issue and in the backlog: the long final calls need a richer rendering (a second voice or a denser source). The 3 rejected calls can come back then.
