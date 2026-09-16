# WP-G2 — (optional) Tier-2 physical labial source

| Field | Value |
|---|---|
| Phase | G — Release & extensions |
| Depends on | B6 |
| Size | M |
| Status default | `optional` (the human sets it to `todo` to schedule it) |

## 1. Goal

Implement a more physical labial model (flapping model of Laje, Gardner & Mindlin 2002) as an alternative source with the same interface as the Tier-1 normal form, build its atlas with the C2 tools, and decide by listening and by fitting a few syllables whether it brings a musical or realism gain worth exposing to users.

## 2. Read first

- `01_research.md` **§5**, §4.3 (for comparison), §4.4
- Reports of WP-B1, WP-C2, and (if done) WP-E4 adequacy report

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/model/labial_source.hpp` | abstract interface (step, x, y, rest state) implemented by Tier-1 and Tier-2 |
| `core/include/cuicui/model/labial_flapping.hpp` | Tier-2 source |
| `params.def` | `engine.source_model` (enum: normal_form, flapping) + Tier-2 constants (expert layer) |
| `python/cuicui/reference/flapping.py` | numpy reference |
| `assets/atlas/source_atlas_flapping_v1.*` | atlas for Tier-2 (if adopted) |
| `core/tests/test_labial_flapping.cpp` | tests |

## 4. Specification

- Equations (research §5):
  $\dot x = y$, $m\dot y = -k(x)x - b(y)y - c x^2 y + a_{lab} p_s \dfrac{\Delta a + 2\tau y}{a_{01} + x + \tau y}$, $k(x) = k_1 + k_2x^2$, $b(y) = b_1 + b_2y^2$.
- **Parameter values:** take them from Laje, Gardner & Mindlin (2002) and/or Mindlin & Laje (2005); record the source (page/table) in the report. Verify by simulation: phonation threshold vs $p_s$; frequency vs $k_1$; waveform shapes.
- Mapping to the common controls: pressure → $p_s$ (linear to the paper's range), tension parameter → $k_1$ (so that the atlas/inverse map machinery works with "β-like" = $k_1$).
- Guard the denominator: if $a_{01} + x + \tau y \le \epsilon$ (labia closing), clamp and treat the flow term as 0 (document; test collision behaviour).
- Integration: RK4 at the internal rate; check convergence as in B1 (the model may be stiffer).
- Atlas and inverse map via C2 scripts parametrised by source model.

## 5. Suggested steps

1. Reference simulation in Python; reproduce the paper's qualitative results.
2. C++ implementation + parity + convergence.
3. Atlas + inverse map for Tier-2; playable range.
4. A/B renders vs Tier-1 on archetypes; fit 10 syllables with E3 tooling (if E-phase done) and compare losses.
5. Recommendation in the report: adopt as user option, keep as expert-only, or drop.

## 6. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Reproduces a documented threshold and frequency behaviour from the source paper (numbers in report, within 5 %). |
| 2 | Parity with numpy reference < 1e-9 relative; convergence < 1 cent at the chosen internal rate. |
| 3 | Stability fuzz 30 s: 0 non-finite resets. |
| 4 | Switching `engine.source_model` at runtime only at note boundaries (no clicks). |
| 5 | A/B listening page and (if applicable) fit comparison table produced; recommendation written. |
| 6 | `scripts/check.sh` passes. |

## 7. Listening & results

`listening/G2/`: same archetype song (C4) with Tier-1 vs Tier-2; onsets comparison; noisy/rough regimes; fitted syllables comparison if available.

## 8. Out of scope

Two-mass model (backlog unless the human asks).
