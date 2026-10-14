# VARY1 — Variations within the bird's style: nudge vs shape model, and new order

> Study protocol, written **before** data collection (pre-registration, WP-R3). Status: **done 2026-10-14, results in §5** (sections 1–4 unchanged since before the test). The pre-registered rule kept nothing; the human decides (§5.3).
> Page: `listening/R3/vary1/index.html` (self-contained) · generator: `scripts/studies/vary1_make.py` (template `scripts/studies/vary1_page.html`, renderer `scripts/studies/r3_render.py`, models `python/cuicui/variation`) · design: `listening/R3/vary1/design.json` (design hash `d57bdc29849e`; banks `hf_22938` `5bc449a1d4c4`, `hf_22937` `39c926b8b0cb`) · analysis: `scripts/studies/vary1_analyze.py <results.json>`.
> Origin: WP-R3 scoping. The human: "the shape model and the new order are my favorite. i have low hope for Blend and we can discard it, and Nudge would probably also work instead of the model. Nudge is less interesting but a safe backup. Nudge and Shape model should be anyway compared blindly."

## 1. Questions

1. Do syllables varied by the **nudge** or by the **shape model** still sound like this bird? At its natural spread (v = 1), and at twice it (v = 2)?
2. Which of the two, compared blindly?
3. Do **new songs** made from the bird's own transitions (new order) still sound like this bird?

## 2. What the bird does (measured, WP-R3)

On 22938 (full recording) and 22937, house finches. The renditions of one syllable type form tight bundles (`build/r3/types_top.png`). Median over 41 types with ≥ 5 renditions (tracker hops excluded):

| | Natural spread |
|---|---|
| note | ±26 c (standard deviation) |
| length | ±2.5 % |
| loudness | ±1.5 dB |
| shape | 52 c RMS beyond the note; 3 smooth modes explain 91 % (the first alone 54 %) |
| gap between two syllables | ±4.3 ms (mean 41 ms) |
| whole song's pitch | ±11 c |

Order:
- 22937 sings fixed songs: 4 song types, each always in the same order (most likely next syllable type: 100 %).
- 22938 has a grammar with branches: every song starts the same way, then branches, skips and recombines sections, and ends at different places (most likely next type: 62–75 %, about 2 successors).

## 3. Method

- **Models** (`cuicui.variation`, fitted per bird on its INGEST1 bank):
  - **Types:** the syllables are clustered by shape and length (median |Δ cents| over 32 time-normalised points + 600 c per e-fold of length, average linkage, cut at 100). Types with ≥ 5 renditions (without tracker hops) get a model: the mean shape (48 points, after removing each rendition's note), 3 shape modes, and the spread of note, length and loudness.
  - **One amount v:**
    - each value (note, length, loudness, and for the shape model the mode scores) moves from the real rendition's value r toward a fresh draw t from its type's spread: r + v·(t − r);
    - v = 0 is the real syllable;
    - v = 1 is a new rendition at the bird's natural spread;
    - v = 2 goes twice as far.
    - Syllables without a type model get a nudge with the bird's median spread.
  - **Nudge:** note, length and loudness only.
  - **Shape model:** also the shape modes. The real rendition's fine detail (what the modes do not explain) is kept.
  - **New order:**
    - a Markov chain over types (order 2, backing off to order 1; or order 1), learned from the bird's real phrases;
    - a song starts where a real one started, follows the transitions the bird used and ends where it ended;
    - each type is sung by a random real rendition, with a real gap;
    - each new song lasts within 30 % of the real song it replaces (the closest of 400 tries).
- **Voice:** the plugin's whistle (sine + 2nd/3rd harmonics at −30/−33 dB, 2.5 ms fades, light room).
  - All whistle versions get the recording's own background (its quietest 1.5 s, looped).
  - They are matched in loudness to the reference on the bird's syllables.
- **Listener:** the human, headphones. MUSHRA-style: each screen has the real recording (Reference) and the versions A, B, C… in random order, with the reference also hidden among them. Ratings are 0–100: "how much does it sound like this bird singing?", where 100 means as natural as the recording.
- **Part A, "the same song again"** (screens A1 and A2, shown first, in random order):
  - **A1:** 22937, 16.15–27.25 s, the same song sung 3 times;
  - **A2:** 22938, 63.80–73.17 s, two renditions of one song.

  Each version sings at the real songs' onsets:

  | Version | What it is |
  |---|---|
  | `real` | the whistle on the bird's own renditions |
  | `same` | the first song's rendition, sung again identically each time (today's repetition) |
  | `nudge1`, `shape1` | that rendition, varied every time, v = 1 |
  | `nudge2`, `shape2` | the same, v = 2 |
  | `anchor` | `real` with strong harmonics |
  | `ref` | the hidden reference |
- **Part B, "new songs"** (screens B1 and B2, after part A, in random order):
  - **B1:** 22938, 43.10–51.70 s, two songs;
  - **B2:** 22938, 139.40–149.20 s, two songs.

  | Version | What it is |
  |---|---|
  | `real` | the whistle on the bird's own songs |
  | `order2` | new songs, order 2 |
  | `order1` | new songs, order 1 |
  | `shuffle` | the real songs' syllables in a random order, same gaps; long calls stay in place |
  | `order2s` | order 2 + shape model v = 1 (the full proposal) |
  | `anchor` | `real` with strong harmonics |
  | `ref` | the hidden reference |
- **Seed** 20261014. The page records ratings, plays per version and time per screen.

## 4. How the results are used (fixed before data)

A version **keeps the bird** if its mean rating over the part's two screens is **≥ 90** and **at least `real` − 10**.

1. **Listener check:** the hidden reference is rated ≥ 90 on every screen. A screen where it is not is reported (and still counted).
2. **Method (part A):**
   - the **shape model** is used if `shape1` keeps the bird and `shape1` ≥ `nudge1` − 5;
   - otherwise the **nudge**, if `nudge1` keeps the bird;
   - if neither does, there is no syllable variation in the plugin, and the human is asked.
3. **Range (part A):** the Variation control goes up to **2** (twice the bird's spread) if the chosen method's v = 2 version keeps the bird; otherwise up to 1. Either way, Variation 1 = the bird's natural spread.
4. **New order (part B):**
   - **kept** if `order2` keeps the bird; the plugin then sings new songs (order 2) as Variation rises;
   - if `order1` also keeps the bird and `order1` ≥ `order2` − 5, order 1 is reported as a possible "wilder" setting (not a default).
5. **Together (part B):** if `order2s` keeps the bird, Variation 1 means new songs + varied syllables. If not, the two are offered separately and the human is asked.
6. **Exploratory (no decision):**
   - `same` vs `real`: does identical repetition sound less alive?
   - `shuffle` vs `real`: is the order audible at all? If `shuffle` keeps the bird too, the new order is kept for variety, not for naturalness.
   - `anchor` should sit at least 20 below `real`; if not, the test discriminated little, and this is reported.

## 5. Results

Listener: the human, Firefox, headphones, 2026-10-14 (9 minutes). Raw data: `studies/data/VARY1/`. Analysis: `vary1_analyze.py` (unchanged since before the test).

### 5.1 Ratings

| Version | A2 (22938) | A1 (22937) | **Part A mean** |
|---|---|---|---|
| hidden reference | **70** | 98 | 84.0 |
| real (the bird's own renditions) | 71 | 98 | **84.5** |
| shape1 (shape model, v = 1) | 70 | 98 | **84.0** |
| nudge1 | 71 | 76 | 73.5 |
| nudge2 | 42 | 82 | 62.0 |
| same (identical repetition) | 56 | 63 | **59.5** |
| shape2 | 29 | 63 | 46.0 |
| anchor | 2 | 5 | 3.5 |

| Version | B2 | B1 | **Part B mean** |
|---|---|---|---|
| hidden reference | 98 | 100 | 99.0 |
| real (the bird's own songs) | 77 | 100 | **88.5** |
| order2 (new songs) | 85 | 73 | **79.0** |
| order2s (new songs + shape v = 1) | 76 | 76 | 76.0 |
| order1 | 59 | 79 | 69.0 |
| shuffle | 32 | 59 | 45.5 |
| anchor | 1 | 3 | 2.0 |

Comment (verbatim): "the quality of the bird sound is not catastrophic but sometimes misses a bit of timbre complexity".

### 5.2 The pre-registered rule

1. **Listener check:** on A2 the hidden reference got **70** (below 90; reported, counted). Every A2 rating is ≤ 71, so the absolute bar of 90 cannot be met on part A.
2. **Method:** no version keeps the bird by the rule. shape1 is 84.0 (≥ real − 10 and ≥ nudge1 − 5, but < 90), and nudge1 is 73.5. → **no syllable variation; ask the human.**
3. **Range:** not applicable.
4. **New order:** order2 79.0 is < 90 (and ≥ real − 10) → **not kept.** order1 69.0.
5. **Together:** order2s 76.0 → offered separately; ask the human.
6. **Exploratory:**
   - same 59.5 vs real 84.5: **identical repetition sounds 25 points less like the bird**;
   - shuffle 45.5: **the order is audible**;
   - the anchor sits 80+ points below real, so the test discriminates.

### 5.3 What it says (the agent's reading, not a pre-registered decision)

- **The repetition problem is real and audible.** Singing the same rendition again (`same`) lost 25 points against the bird's own renditions on both screens (56 vs 71, 63 vs 98).
- **The shape model at the bird's own spread removes it.** It equals the real renditions on both screens (70 vs 71, 98 vs 98). The nudge gets only part of the way (73.5), mainly on A1 (76 vs 98).
- **Twice the bird's spread is too much** for both methods (shape2 46, nudge2 62). The bird's own spread is the limit.
- **New songs are close to, but below, real songs.** order2: B2 85 (above real 77), B1 73 (below real 100). Order 1 is freer and worse (69). A random order is far worse (45.5).
- **The absolute bar failed for a reason outside the methods:** on A2 the real recording itself was rated 70.
