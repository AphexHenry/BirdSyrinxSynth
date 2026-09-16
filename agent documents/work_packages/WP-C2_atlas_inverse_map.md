# WP-C2 — Source atlas & inverse pitch map

| Field | Value |
|---|---|
| Phase | C — Playability |
| Depends on | B6 |
| Size | M |
| Milestone | – |

## 1. Goal

Make the bird play **in tune** without losing its physics. Precompute an **atlas** of the labial oscillator's behaviour over (α, β), including onset/offset boundaries, then build the **inverse map** that turns a target pitch and a "Tone" value (rich ↔ pure) into (α, β). Ship it as a compact table compiled into the core, with a real-time-safe lookup. Verify tuning accuracy in closed loop.

## 2. Read first

- `01_research.md` **§4.3 (all)**, **§7.4**, §4.9, §7.3
- `02_architecture.md` §6.1 (`source_only`), §9.3 (atlas files)
- Reports of WP-B1 (regime maps, onset table) and WP-B6

## 3. Deliverables

| Path | Content |
|---|---|
| `python/cuicui/atlas/source_atlas.py`, `__main__.py` | atlas generation (parallel, resumable) |
| `python/cuicui/atlas/inverse.py` | inverse tables from the atlas |
| `assets/atlas/source_atlas_v1.json` + `.bin` | atlas and inverse tables (committed, ≤ 5 MB) |
| `scripts/gen_atlas_header.py` | → `core/include/cuicui/generated/atlas_tables.hpp` |
| `core/include/cuicui/control/atlas.hpp`, `inverse_map.hpp` | runtime lookup |
| `python/bindings/module.cpp` | `inverse_solve(nu, tone, alpha_breath, physics)` |
| `core/tests/test_inverse_map.cpp`, `python/tests/test_atlas.py` | tests |
| `params.def` | add `perf.range_fold` |

## 4. Specification

### 4.1 Forward atlas (γ₀ = 24 000, internal rate 352.8 kHz for accuracy, noise off)

- Grid: α ∈ [−0.10, 0.60] step 0.005 (141), β ∈ [−0.30, 1.20] step 0.005 (301).
- Two initial-condition protocols per grid point:
  - **from rest:** start at `rest_state(−0.1, β)`, ramp α from −0.1 to target over 20 ms (≈ respiratory attack), then hold 150 ms, analyse the last 100 ms;
  - **from oscillation:** start from the steady oscillation at (α = 0.45, β), ramp α down to target over 20 ms, hold, analyse.
- Per point and protocol: `nu` = 2π f₀/γ₀ (NaN if silent), `amp` (peak-to-peak x), `sci`, `regime` ∈ {0 silent, 1 periodic, 2 period-doubled, 3 irregular} — periodic if normalised autocorrelation at the period > 0.99; period-doubled if the peak at 2T exceeds the one at T by a margin and energy at f₀/2 > −20 dB; irregular otherwise.
- **Boundaries per β:** `alpha_on(β)` by slow ramp up from rest (0.2 s⁻¹, B1 detector) and `alpha_off(β)` by slow ramp down from oscillation.

### 4.2 Inverse tables

- ν grid: 256 log-spaced values from 0.03 to 1.10.
- For each α row (from-rest protocol, periodic cells only): ν(β) must be monotone increasing; resolve small violations by keeping the longest monotone run containing the most periodic cells; report violations.
- `beta_of_alpha_nu[α_i][ν_j]`: monotone (PCHIP or linear) inversion; unreachable → sentinel NaN.
- `alpha_lo_of_nu[ν_j]`: smallest α on the grid where ν is reachable **and** α ≥ `alpha_on`(β(α, ν)) + δ, δ = 0.01.
- `alpha_hi_of_nu[ν_j]`: largest α ≤ 0.60 where ν is reachable with β ≤ 1.2.
- `sci_of_alpha_nu` (for UI/fitting hints).
- Smooth `alpha_lo/hi` along ν with a 5-point median + 3-point mean (report the effect).

### 4.3 Runtime API

```cpp
namespace cuicui::control {
struct InverseResult { double alpha, beta, nuUsed; bool reachable, folded, clamped; };
class InverseMap {
public:
    // nu = 2π f0 / gamma (gamma already includes body size)
    InverseResult solve(double nu, double tone, double alphaBreath, double physics, bool rangeFold) const noexcept;
    double nuMin() const noexcept; double nuMax(double tone) const noexcept;
};
}
```

- α_T = α_lo(ν) + tone·(α_hi(ν) − α_lo(ν)); α = (1 − physics)·α_T + physics·α_breath; clamp α into [α_lo, α_hi]; β = bilinear lookup in (α, log ν).
- Out of range: if `rangeFold` → shift ν by octaves (×2 or ÷2) until reachable (`folded` = true); else clamp to nearest reachable (`clamped` = true).
- Lookups are O(1) (index computation + bilinear interpolation), `noexcept`, no allocation.

### 4.4 Playable range report

For σ ∈ {0.25, 0.5, 1, 2, 4} and tone ∈ {0, 0.5, 1}: lowest and highest reachable pitch as **note names** (e.g. "G5 – B7") — a table for musicians, in the report and later in the manual.

## 5. Suggested steps

1. Atlas generation script (C++ via `source_only`; multiprocessing; cache per β row) → figures.
2. Boundaries, inverse tables, monotonicity report.
3. Binary + JSON + header generation; C++ `InverseMap`.
4. Closed-loop tuning tests (Python driver with `render_controls` and C++ unit tests on table consistency).
5. Listening renders (Python driver: melody → per-note ν → solve → controls → full voice).

## 6. Out of scope

MIDI/plugin integration (C3), ornaments, side-switch decisions, the fitting atlas of the full voice (E2).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Atlas consistency: at the B1 spot points (B1 test #2) the atlas ν matches B1 frequencies within 0.5 %. |
| 2 | Boundaries: `alpha_on` matches the analytic onset-from-rest curve (research §4.3.1) within ±0.01 for β < 0.25. |
| 3 | Monotonicity violations of ν(β) affect < 2 % of periodic cells (report map). |
| 4 | **Closed-loop tuning** (full voice, noise off, σ = 1): for every semitone in the reachable range at tone ∈ {0.2, 0.6, 1.0}, steady-state f₀ error ≤ 5 cents (median ≤ 2 cents). At tone 0.05: ≤ 15 cents. |
| 5 | Same as #4 at σ = 0.5 and σ = 2 (subset: every 3rd semitone). |
| 6 | Physics: at a fixed target note, α_breath swept over its range with physics = 1 → f₀ stays within 5 cents while SCI changes (report range). |
| 7 | `solve()` benchmark < 100 ns per call, zero allocations; table header ≤ 5 MB; compile time increase reported. |
| 8 | Folding: a note one octave above the max reachable pitch returns `folded = true` and a ν exactly half the request. |
| 9 | `scripts/check.sh` passes. `state.md` baseline "Tuning error" filled. |

## 8. Listening & results

Render into `listening/C2/` (48 kHz, full voice, Python driver; notes 300 ms with 80 ms gaps, respiratory attack/release as default).

| File | Content | Listen for |
|---|---|---|
| `01_chromatic_tone02.wav`, `02_chromatic_tone06.wav`, `03_chromatic_tone10.wav` | chromatic scale over the playable range | in tune; timbre from rich/reedy (0.2) to whistle (1.0) |
| `04_tone_morph_fixed_pitch.wav` | one sustained note, tone 0 → 1 → 0 over 6 s | **pitch stays still while the timbre morphs** (key result) |
| `05_physics_breath.wav` | sustained note, physics = 1, breath oscillating slowly | timbre breathes with pressure, pitch stays |
| `06_melody_size1.wav`, `07_melody_size2.wav` | a short traditional melody (e.g. "Frère Jacques") at σ = 1 and σ = 2 | recognisable tune, bird voice; σ = 2 lower and larger |
| `08_folding.wav` | scale going beyond the top of the range with folding on | notes above the range drop an octave instead of failing |

Figures: regime map from the atlas with `alpha_on/off`; inverse tables (β over α × ν); playable range table; tuning error plots.

## 9. Pitfalls

- Use the **from-rest** protocol for the tone lower bound, otherwise notes can fail to start after silence.
- ν depends on γ, which depends on size: always compute ν with the *current* γ.
- Near the SNIC boundary tiny α errors cause large f₀ changes: this is why δ and the lower-tone tolerance exist; don't push tone 0 to the exact boundary.
