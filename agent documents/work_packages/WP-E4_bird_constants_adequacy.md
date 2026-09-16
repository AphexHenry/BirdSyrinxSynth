# WP-E4 — Per-bird constants + model adequacy report

| Field | Value |
|---|---|
| Phase | E — Fitting (regression) |
| Depends on | E3 |
| Size | M |
| Milestone | **M5 — real zebra finch resynthesis** |

## 1. Goal

Fit the **anatomical constants of each bird** (trachea, damping, time scale, noise levels…) shared across all its syllables, test the open modelling hypotheses (sign of the tracheal reflection, need for two sources), re-run the refinement with the fitted constants, and write an honest **model adequacy report**: what the Tier-1 model reproduces well, what it cannot, and which extensions would help.

## 2. Read first

- `01_research.md` **§8.4 (Stage 5)**, §4.6 (trachea sign), §4.4 (two sides), §10 (risks)
- Reports of WP-E3 (metrics, human ratings), WP-D1/D2 (observations, two-voice flags), WP-B2 (human's reflection-sign preference)

## 3. Deliverables

| Path | Content |
|---|---|
| `python/cuicui/fit/constants.py` | alternating optimisation of constants and controls |
| `python/cuicui/fit/twovoice.py` | two-source fits for flagged syllables |
| `python/cuicui/fit/adequacy.py` | analysis & figures for the adequacy report |
| `data/fits/<run_id>_final/` | final fits for all syllables (NOT committed) |
| `assets/birds/<bird>.settings.json` | fitted constants per bird (committed, small) |
| `agent documents/reports/WP-E4_report.md` | includes the **Model adequacy report** section |

## 4. Specification

### 4.1 Constants φ (per bird)

| Constant | Range | Notes |
|---|---|---|
| `trachea.length_mm` | 20 – 50 | expected near 33 |
| `trachea.reflection` | −0.95 – 0.95 | sign hypothesis test (§4.3) |
| `trachea.loss` | 0 – 1 | |
| `oec.damping` | 0.02 – 0.8 | |
| `beak.damping` | 0.05 – 1 | |
| `src.gamma` | 21 000 – 26 000 | |
| `noise.beta_sigma` | 0 – 0.05 | informed mostly by noisy/buzzy syllables |
| `noise.aspiration` | 0 – 0.5 | |

Per-clip output gain remains analytic (E1).

### 4.2 Alternating optimisation

1. Pick representative syllables per bird: for each label, the syllable whose E3 feature vector is closest to the label median (and one extra for labels with high variance); max 12.
2. **(a) Constants step:** CMA-ES over φ (8 dims, population 16, ≤ 150 generations) with the representatives' controls fixed (E3 solutions); objective = sum of E1 losses over representatives. Note: when γ changes, re-solve β with pitch locking (controls are expressed in tone/δ, so this is automatic).
3. **(b) Controls step:** re-run E3 refinement on the representatives, warm-started from their current solutions, with the new φ (regenerate the E2 voice atlas for φ only if the E3 loss worsens > 5 % without it — document).
4. Iterate (a)–(b) up to 4 times; stop when the summed loss improves < 2 %.
5. Final: E3 refinement on **all** syllables of the bird with φ*, warm-started from `zf_e3`.

### 4.3 Hypothesis tests

- **Reflection sign:** run §4.2 twice, initialised with r = +0.5 and r = −0.5 (other constants default); compare converged losses and listen. Report both, choose the better (and compare with the human's B2 preference).
- **Two sources:** for syllables flagged two-voice by D2 (and the 10 worst single-side fits), fit with both sides: second side controls `gate_R(t)`, `tone_R(t)`, `δ_R(t)` initialised from D2's `f0b_hz` track; compare loss with single-side fit; keep the two-source fit only if loss improves > 10 %.

### 4.4 Model adequacy report (section in the WP report)

1. **Overall numbers:** per label and per bird — loss/f₀/entropy/harmonic errors for E3 vs E4 final; distribution plots.
2. **What works:** labels and sound types with good resynthesis (with human ratings from E3 feedback + your own listening notes).
3. **What does not work:** concrete, per phenomenon (e.g. "noisy note onsets lack broadband transient", "upper harmonics 5–8 kHz too weak in stacks", "rapid FM > 50 Hz not followed"), each with evidence (figures, example ids).
4. **Hypotheses** for each failure (model structure vs fitting vs data quality) and **proposed extensions** (e.g. source–tract interaction, different noise model, Tier-2 source (G2), time-varying trachea loss) → append to `backlog.md`.
5. **Fitted constants**: table per bird with plausibility comments (e.g. trachea length vs the ~33 mm literature value).
6. **Recommendation** for the gesture phase: which labels are reliable enough to learn from; which should be excluded or down-weighted.

## 5. Suggested steps

1. Representatives selection; constants step on one bird; verify improvement.
2. Sign hypothesis runs (can run in parallel).
3. Two-source fits on flagged syllables.
4. Final all-syllable refinement per bird (background).
5. Adequacy analysis, figures, report; listening pages.

## 6. Out of scope

Gesture extraction (F1). Implementing model extensions (only propose them).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Constants step improves the representatives' summed loss vs defaults (report %, no fixed threshold; if < 5 % explain why). |
| 2 | Final all-syllable median loss ≤ E3 median (per bird); if not, explain and keep E3 fits as final. |
| 3 | Both reflection-sign runs completed and compared (losses + listening notes). |
| 4 | Two-source comparison done for all flagged syllables (table). |
| 5 | Fitted constants are within ranges and saved to `assets/birds/<bird>.settings.json` (schema-free JSON of parameter IDs → values, with provenance). |
| 6 | Model adequacy report complete (all 6 subsections), with ≥ 8 figures and example ids. |
| 7 | `scripts/check.sh` passes. |

## 8. Listening & results — milestone M5

- `listening/E4/`: per label: real vs E3 vs E4 final (3 median examples); reflection sign A/B (same syllables, both constant sets); two-source vs single-source for flagged syllables; one complete motif per bird: real vs concatenated resynthesised syllables (with real gaps).

Final message: **milestone M5**. Ask the human for a careful listening session and feedback on: overall realism per label, the most disturbing artefacts, and whether any labels should be excluded from learning.

## 9. Pitfalls

- Constants and controls can trade off (e.g. tract damping vs tone): keep controls smooth (smoothness weight) and constants shared across many syllables to break the ambiguity.
- Changing γ changes the playable ν range: check that no syllable becomes unreachable.
- Don't overfit constants to a single label: representatives must cover all labels of the bird.
