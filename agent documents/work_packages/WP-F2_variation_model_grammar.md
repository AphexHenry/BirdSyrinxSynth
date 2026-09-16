# WP-F2 — Variation model (FPCA), syllable grammar, voice bank export

| Field | Value |
|---|---|
| Phase | F — Voice bank & variation |
| Depends on | F1 |
| Size | M |
| Milestone | – |

## 1. Goal

Learn *how* the real birds vary: a statistical model per gesture type (functional PCA of the registered curves + duration/anchor distributions), syllable compositions, a probabilistic **grammar** of syllable sequences, and pitch couplings. Export everything as a **voice bank** file per bird. Provide a Python reference generator (the ground truth for the C++ runtime in F3) and compare generated songs with real songs.

## 2. Read first

- `01_research.md` **§7.7–§7.8**
- `02_architecture.md` **§9.5** (voice bank format)
- Reports of WP-F1 (clusters, registration), WP-D1 (annotation sequences), WP-C5 (runtime behaviour of Variation, Motion, Density, Speed)

## 3. Deliverables

| Path | Content |
|---|---|
| `python/cuicui/gestures/fpca.py` | FPCA per gesture type |
| `python/cuicui/gestures/grammar.py` | syllable types, bouts, Markov grammar, gaps |
| `python/cuicui/gestures/couplings.py` | amplitude–pitch and OEC tracking fits |
| `python/cuicui/gestures/bank.py` | voice bank export + validation |
| `python/cuicui/gestures/generate.py` | **reference generator** (Python) using a xoshiro128+ / Box–Muller implementation identical to the C++ one |
| `schemas/voice_bank.schema.json` | schema (§9.5) |
| `assets/voicebanks/zebra_finch_<bird>/bank.json` (+ `examples/`) | voice banks (committed; ≤ 2 MB each) |
| `python/tests/test_bank_*.py` | tests |

CLI:
```bash
uv run python -m cuicui.gestures bank --in data/gestures/zf_v1 --bird bird01 --out assets/voicebanks/zebra_finch_bird01
uv run python -m cuicui.gestures generate --bank assets/voicebanks/zebra_finch_bird01 --variation 1 --seconds 20 --seed 3 -o song.gscore.json
```

## 4. Specification

### 4.1 FPCA per gesture type

- Input: registered curves (F1) of all members: channels pitch (cents), amp (dB), tone, side; N = 64.
- Standardise channels with scales $s_P$ = 100 cents, $s_A$ = 6 dB, $s_T$ = 0.2, $s_S$ = 0.5 (stored as `channel_scale` for documentation); compute mean and PCA (SVD) on the concatenated standardised vectors.
- Keep K modes with cumulative explained variance ≥ 95 %, K ≤ 8; store **components in natural units** and coefficient variances (`variance`), per §9.5 generation rule.
- Clusters with < 10 members: K ≤ 2.
- Store per type: duration log-normal (μ, σ of log ms), anchor offset (mean, std, cents rel. syllable anchor), movement score, hint medians (oec_ratio, gape), count.

### 4.2 Syllable types

- One syllable type per annotation label (reliable labels only).
- Gesture sequences: list of cluster-id sequences observed for that label with probabilities (keep those covering ≥ 90 % of instances; others folded into the most similar sequence), intra-syllable gaps (ms).
- Syllable anchor relative to motif anchor (motif anchor = median pitch of the bird's motif).

### 4.3 Grammar

- **Bouts:** consecutive annotated segments of a recording with gaps < 300 ms.
- States: `START`, `intro` (if introductory-note labels exist), each syllable label, `END`.
- First-order Markov transition matrix with add-0.5 smoothing over observed transitions only (unobserved transitions stay 0 unless Variation smoothing at runtime).
- Gap distributions: log-normal per transition type (intro→intro, intro→syllable, syllable→syllable, syllable→END).
- Intro repeats distribution.

### 4.4 Couplings

- `amp_per_octave_db`: robust regression of gesture amplitude (dB) on pitch (octaves rel. anchor) within gestures.
- `oec_tracking`: slope κ of log f_H vs log f₀ across fitted frames (voiced); `gape_tracking` likewise if gape varied in fits.

### 4.5 Reference generator (Python)

Implements exactly the runtime rules of research §7.8 and WP-C5 §4.1, but with voice-bank sources:
- syllable choice by grammar with Variation smoothing $P' = (1 - 0.2v)P + 0.2v\,U$;
- gesture curves $c = \mu + \sum_k z_k \phi_k$, $z_k = v\sqrt{\lambda_k}\,\xi_k$, ξ truncated ±2.5; invention for v > 1: interpolate μ towards another type's mean with weight (v − 1)·0.3;
- Range multiplies pitch deviations; Speed divides durations; Density scales gaps; Motion biases types by movement score (same formula as C5).
- Uses the C++-identical PRNG so F3 can prove bit-level parity of the random draws.
- Output: gesture score (events) renderable with the C4 player.

### 4.6 Evaluation

- **Generalisation:** 5-fold cross-validation of FPCA reconstruction error (held-out vs train, ratio ≤ 1.2).
- **Distribution match:** for durations, pitch ranges, FM rates, tone distributions: Wasserstein distance between generated (Variation 1, 200 s) and real, compared with the distance between two halves of the real data (report ratio; target ≤ 2).
- **Stereotypy:** at Variation 0, ≥ 90 % of generated bouts reproduce the bird's most frequent motif order.
- **Perceptual proxy (optional):** if a bird-sound embedding model (e.g. Perch or BirdNET) is installable offline, report embedding distances real↔generated vs real↔real.

## 5. Suggested steps

1. FPCA per type + CV.
2. Syllable types, bouts, grammar, gaps.
3. Couplings.
4. Bank export + schema + validation.
5. Reference generator + evaluation + listening.

## 6. Out of scope

C++ runtime (F3), UI (F4), neural generative models (backlog).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Bank files validate against the schema; generation rule reconstructs each member's registered curve from its projected coefficients with the explained-variance error. |
| 2 | FPCA CV ratio ≤ 1.2 for types with ≥ 20 members (report all). |
| 3 | Grammar rows sum to 1 (±1e-9); Variation-smoothed rows too. |
| 4 | PRNG parity: Python xoshiro128+ and Box–Muller produce the same first 10 000 values as the C++ implementation (golden file from a C++ test). |
| 5 | Distribution-match ratios reported; stereotypy at Variation 0 ≥ 90 %. |
| 6 | Voice bank size ≤ 2 MB per bird. |
| 7 | `scripts/check.sh` passes. |

## 8. Listening & results

- `listening/F2/`: per bird: 3 real bouts vs 3 generated bouts at Variation 0, 1, 2 (group), and "gesture type variations": for 4 types, the mean gesture and ±2σ along modes 1 and 2.
- Figures: FPCA mode shapes for major types; grammar graph (transition diagram); distribution comparisons.

Ask the human: at which Variation does the generated song stop sounding like the real bird? Does Variation 2 sound interesting or broken?

## 9. Pitfalls

- Registration choices (F1) dominate FPCA quality; if modes look like time shifts, registration failed — fix in F1 code (record the change).
- Don't let rare labels create huge grammar branches; keep a minimum count threshold (≥ 3 observations) or fold them.
