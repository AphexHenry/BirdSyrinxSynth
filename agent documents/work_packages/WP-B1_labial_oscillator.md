# WP-B1 — Labial oscillator (Tier-1 normal form)

| Field | Value |
|---|---|
| Phase | B — The bird body |
| Depends on | A2 |
| Size | M |
| Milestone | – |

## 1. Goal

Implement the heart of the instrument: the **labial oscillator** of one side of the syrinx, as the Takens–Bogdanov normal form, integrated with RK4 at the internal rate. Plug it into `BirdVoice` (replacing the test source), verify its dynamics against analytic results, and produce the first "raw syrinx" sounds: soft (Hopf) and rich (SNIC) onsets, pitch sweeps, and the regime map.

## 2. Read first

- `01_research.md` §2.2–§2.3, **§4.3 (all)**, §4.1, §6
- `02_architecture.md` §5.1, §6, §7.1, §10
- Report of WP-A2

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/model/labial_oscillator.hpp` | `LabialOscillator` |
| `core/include/cuicui/dsp/dc_blocker.hpp` | one-pole DC blocker (cutoff Hz, at internal rate) |
| `core/include/cuicui/model/bird_voice.hpp` | voice now: pressure → α → oscillator L → DC block → gain → decimation |
| `core/include/cuicui/params.def` | add `resp.pressure`, `src.gamma`, `src.L.beta`, `src.L.alpha_offset` (ranges/defaults from `02_architecture.md` §7.1) |
| `core/tests/test_labial_oscillator.cpp` | tests §7 |
| `python/bindings/module.cpp` | add `source_only(alpha, beta, gamma, internal_rate, x0, y0)` (arrays per internal sample) and `rest_state(alpha, beta)` |
| `python/cuicui/reference/labial.py` | slow numpy RK4 reference |
| `python/cuicui/analysis/bifurcation.py` | regime maps, analytic curves, onset/offset ramps (plots) |
| `python/tests/test_labial_parity.py` | parity C++ vs reference |

## 4. Specification

### 4.1 Equations (this project's convention — do not change signs)

$$
\dot x = y,\qquad
\dot y = -\alpha\gamma^2 - \beta\gamma^2 x - \gamma^2 x^3 - \gamma x^2 y + \gamma^2 x^2 - \gamma x y
$$

### 4.2 Class

```cpp
namespace cuicui::model {
class LabialOscillator {
public:
    void prepare(double internalRate) noexcept;          // h = 1/internalRate
    void reset(double x0, double y0) noexcept;
    void resetToRest(double alpha, double beta) noexcept; // stable fixed point, see 4.3
    // One RK4 step of length h with parameters held constant during the step.
    void step(double alpha, double beta, double gamma) noexcept;
    double x() const noexcept; double y() const noexcept;
    uint32_t nonFiniteResets() const noexcept;
};
}
```

- RK4 with $h = 1/f_{int}$; parameters sample-and-hold within a step.
- Guard: if `!std::isfinite(x) || std::abs(x) > 10` → `resetToRest(alpha, beta)`, increment counter.

### 4.3 Rest state

Fixed points solve $x^3 - x^2 + \beta x + \alpha = 0$. Compute the real roots (closed-form cubic or robust Newton with bracketing), keep the **stable** ones ($\mathrm{tr} = -\gamma x(1+x) < 0$ and $\beta + 3x^2 - 2x > 0$), return the **largest** stable root; if none, return $x = 0$. (At α_rest = −0.1 this is the upper branch, $x \approx 1$.)

### 4.4 Voice chain in this WP

- $\alpha = \alpha_{rest} + p\,(\alpha_{max} - \alpha_{rest}) + \alpha^{off}_L$ with $p$ = `resp.pressure` (no dynamics yet — B4 adds them), α_rest = −0.10, α_max = 0.45.
- β = `src.L.beta`, γ = `src.gamma`.
- Output (internal rate): $0.5\cdot \mathrm{DCblock}_{20\,Hz}(x)\cdot 10^{g/20}$ with g = `out.gain_db`, then decimation.
- Controls are ramped per internal sample (A2 infrastructure).

### 4.5 Bifurcation analysis script (Python, offline)

`uv run python -m cuicui.analysis.bifurcation --out listening/B1/figs` produces:
1. **Regime maps** over α ∈ [−0.1, 0.5] (≥ 61 pts), β ∈ [−0.3, 1.0] (≥ 66 pts), γ = 24000, internal rate 192 kHz, 0.15 s per point starting from `rest_state(-0.1, β)` then jumping to α (analyse last 0.1 s): heatmaps of f₀ (Hz), peak-to-peak amplitude, SCI = spectral centroid / f₀; silent cells masked.
2. **Analytic overlay:** Hopf line (α = 0, β > 0); saddle-node curve $(\alpha,\beta) = (2x^3 - x^2,\ 2x - 3x^2)$ for x ∈ [−0.5, 1.2]; onset-from-rest curve (§4.3.1 of research).
3. **Ramp experiments:** for β ∈ {−0.10, −0.05, 0.05, 0.15, 0.20, 0.30, 0.50}: ramp α from −0.1 to 0.4 at 0.2 s⁻¹ from rest (onset = first 10 ms window with ≥ 3 zero crossings of DC-removed x and peak-to-peak > 0.05), then ramp back down (offset). Table + hysteresis plot.
4. f₀ vs β at α ∈ {0.05, 0.2, 0.4}, with the approximation $\gamma\sqrt\beta/2\pi$ overlaid.

Use `cuicui_core.source_only` for speed (GIL released; parallelise over points with multiprocessing).

## 5. Suggested steps

1. `LabialOscillator` + rest state + unit tests on fixed points.
2. Frequency/onset tests (§7) in C++ (helper: zero-crossing frequency estimator with linear interpolation).
3. Voice integration + CLI renders.
4. Bindings `source_only`, `rest_state`; Python reference + parity test.
5. Bifurcation script, plots; listening renders.

## 6. Out of scope

Trachea, vocal tract, noise, respiratory dynamics, second side, size scaling, atlas tables (C2).

## 7. Tests & acceptance criteria

All with γ = 24 000 and internal rate 192 kHz unless stated; frequencies measured over 0.2 s after 50 ms settling, starting from `resetToRest(-0.1, β)` then stepping α to the target.

| # | Criterion |
|---|---|
| 1 | Rest state: for (α, β) ∈ {(−0.1, 0.05), (−0.05, 0.3), (−0.1, 0.8)} the returned x is a root (residual < 1e-9) and stable; simulated for 50 ms it does not move by more than 1e-6. |
| 2 | Frequencies (±0.5 %): (α, β) = (0.10, 0.50) → 3098 Hz; (0.20, 0.20) → 2658 Hz; (0.30, 0.10) → 2817 Hz; (0.15, 0.05) → 1380 Hz; (0.40, 0.35) → 3797 Hz; (0.25, −0.05) → 1740 Hz. |
| 3 | Hopf approximation: α = 0.005, β ∈ {0.3, 0.5, 0.8}: f₀ within 3 % of $\gamma\sqrt\beta/2\pi$. |
| 4 | Silence: (α, β) = (−0.05, 0.3) and (0.05, 0.05) from rest → peak-to-peak of x (after 50 ms) < 1e-3. |
| 5 | Onset from rest (slow ramp 0.2 s⁻¹): β ∈ {−0.10, −0.05, 0.05, 0.15, 0.20}: onset α within ±0.01 of 0.217, 0.182, 0.116, 0.054, 0.026 (+ ramp lag, see research §4.3.1). β ∈ {0.30, 0.50}: onset α ∈ [−0.005, 0.04]. |
| 6 | Frequency scales with γ: (0.2, 0.2) at γ = 12 000 gives half the frequency of γ = 24 000 within 0.2 %. |
| 7 | Convergence: pitch difference between internal rates 176.4 kHz and 1.4112 MHz (8×) < 1 cent at the points of #2. |
| 8 | Stability fuzz: 10 s of random smooth parameter trajectories within registry ranges (α via pressure + offset, β, γ): 0 non-finite resets, no NaN in output. |
| 9 | Parity: C++ `source_only` vs numpy reference, 0.1 s, 3 parameter sets: max abs difference < 1e-9. |
| 10 | Zero allocations in `BirdVoice::process()`. `scripts/check.sh` passes. |

## 8. Listening & results

Render into `listening/B1/` (48 kHz; peak-normalised unless noted). All sounds are the **raw source** (no tract yet) — they are expected to sound electronic.

| File | Content | Listen for |
|---|---|---|
| `01_hopf_onset_beta05.wav` | β = 0.5, pressure ramp 0 → 0.4 over 1 s, hold 1 s, ramp down | tone appears softly and grows; almost pure |
| `02_snic_onset_beta005.wav` | β = 0.05, pressure ramp to reach α ≈ 0.3 over 1 s | tone appears abruptly, *low and buzzy* at onset, rising in pitch and getting purer as α grows |
| `03_beta_sweep_alpha02.wav` | α = 0.2 fixed, β −0.05 → 0.8 over 4 s | pitch glide ~1.3 → 3.5 kHz; timbre goes from reedy to whistle |
| `04_alpha_sweep_beta02.wav` | β = 0.2, α 0.03 → 0.45 over 4 s | pitch rises with pressure too (coupling you will later compensate) |
| `05_hysteresis_beta015.wav` | β = 0.15, α slowly up then down (4 s each) | sound starts and stops at different pressure (not normalised) |
| `06_fast_beta_trill.wav` | α = 0.25, β = 0.3 + 0.2·sin(2π·20 Hz·t), 2 s | clean 20 Hz trill: the oscillator follows fast tension changes |
| `07_gamma_half.wav` | same as 03 with γ = 12 000 | same shape one octave lower |

Figures on the page and in the report: regime maps with analytic overlay, onset/offset table and hysteresis plot, f₀ vs β.

## 9. Pitfalls

- Signs: papers differ. Use §4.1 exactly; tests #2–#5 will catch sign mistakes.
- Starting from x = 0 instead of the rest state changes results in the bistable region (β between 0 and 0.25). Always start from rest for tests.
- The oscillator output has a large DC component near onset (mean x ≈ 0.3) → DC blocker required before any gain.
- Keep `double` for state; float accumulates error at these γ values.
