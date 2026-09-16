# WP-B3 — Upper vocal tract: glottis – OEC – beak + radiation

| Field | Value |
|---|---|
| Phase | B — The bird body |
| Depends on | B2 |
| Size | M |
| Milestone | **M1 — first complete bird sound** |

## 1. Goal

Add the upper vocal tract: the glottis, the oropharyngeal–esophageal cavity (OEC, a Helmholtz resonator the bird tunes while singing), the beak opening, and sound radiation. After this WP the CLI renders the **first complete anatomical bird sound**. Calibrate levels so all later work starts from sensible loudness.

## 2. Read first

- `01_research.md` §2.5–§2.6, **§4.7 (all)**, §4.8, §6
- `02_architecture.md` §6, §7.1, §10, §11
- Reports of WP-B1, WP-B2; `data/manifest/zf_v1_summary.md` and the "Observations" of WP-D1 (to pick reference clips)

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/model/upper_vocal_tract.hpp` | `UpperVocalTract` (state-space, trapezoidal) |
| `core/include/cuicui/model/bird_voice.hpp` | chain: source → trachea → UVT → output mix (flow/radiation) → DC block → gain → decimation |
| `params.def` | add `oec.freq_hz`, `oec.damping`, `beak.gape`, `beak.damping`, `out.radiation` (and `out.gain_db` if not yet) |
| `core/tests/test_upper_vocal_tract.cpp` | tests §7 |
| `python/cuicui/reference/uvt.py` | numpy reference + analytic transfer functions (circuit form **and** published form) |
| `python/cuicui/analysis/uvt_response.py` | response plots and the peak/Q table |
| `python/tests/test_uvt_equivalence.py` | published equations ≡ circuit (analytic) |

## 4. Specification

### 4.1 Physical parameters (research §4.7.3)

- ρ = 1.15 kg/m³, c = 350 m/s; glottis ℓ_g = 2 mm, radius 0.4 mm; beak ℓ_b = 8 mm, $A_b = A_0 + g(A_1 - A_0)$ with $A_0$ = 1 mm², $A_1$ = 20 mm², g = `beak.gape`.
- $L_g = \rho\ell_g/A_g$, $L_b = \rho\ell_b/A_b$, $L_{eq} = L_gL_b/(L_g+L_b)$.
- $C_h = 1/\big((2\pi f_H)^2 L_{eq}\big)$ with $f_H$ = `oec.freq_hz`.
- $Z_0 = \sqrt{L_{eq}/C_h}$, $R_h = z_h Z_0$ (`oec.damping`), $R_b = z_b Z_0$ (`beak.damping`).
- A `sizeScale` argument (default 1) is accepted now and used in B6: lengths ∝ σ, areas ∝ σ², $f_H$ ∝ 1/σ.

### 4.2 State-space and discretisation

States $\mathbf q = [i_g, v_c, i_b]^T$, input $u = p_t$:

$$
\mathbf A = \begin{bmatrix} -R_h/L_g & -1/L_g & R_h/L_g \\ 1/C_h & 0 & -1/C_h \\ R_h/L_b & 1/L_b & -(R_h+R_b)/L_b \end{bmatrix},\quad
\mathbf b = \begin{bmatrix} 1/L_g \\ 0 \\ 0 \end{bmatrix}
$$

Trapezoidal rule with $h = 1/f_{int}$:
$\mathbf q[n+1] = \mathbf M^{-1}\big(\mathbf N\,\mathbf q[n] + \tfrac{h}{2}\mathbf b\,(u[n] + u[n+1])\big)$, $\mathbf M = \mathbf I - \tfrac h2\mathbf A$, $\mathbf N = \mathbf I + \tfrac h2\mathbf A$.

- Recompute $\mathbf M^{-1}$ and $\mathbf N$ (3×3, closed-form inverse) only when parameters changed by more than a tiny epsilon, at most once per internal sub-block of 8 samples.
- Outputs: flow $U_b = i_b[n]$; radiation $\dot U_b = \big(v_c + R_h i_g - (R_h+R_b)\,i_b\big)/L_b$ evaluated from the current state.

### 4.3 Class

```cpp
namespace cuicui::model {
struct UvtOut { double flow; double dflow; };
class UpperVocalTract {
public:
    void prepare(double internalRate) noexcept;
    void reset() noexcept;
    void setParams(double oecFreqHz, double oecDamping, double beakGape, double beakDamping, double sizeScale = 1.0) noexcept;
    UvtOut process(double pt) noexcept;
    double impliedOecVolumeCm3() const noexcept;   // for UI/sanity
};
}
```

### 4.4 Output stage and calibration

$y = \mathrm{DCblock}\big((1-\rho)\,k_f\,U_b + \rho\,k_d\,\dot U_b\big)\cdot 10^{g/20}$, ρ = `out.radiation`.

Calibration constants $k_f, k_d$ (compile-time constants, documented): choose them so that the **reference note** — pressure giving α = 0.2, β = 0.2, trachea defaults (L = 33 mm, r = decided default, ℓ = 0.3), OEC 3000 Hz, gape 0.5, dampings default — peaks at about −6 dBFS at host rate for ρ = 1, and ρ = 0 is within ±3 dB RMS of ρ = 1.

## 5. Suggested steps

1. Python: analytic transfer functions (circuit + published form), equivalence test, response exploration (peak frequency and Q over the ranges).
2. C++ `UpperVocalTract` + response tests vs analytic.
3. Voice integration, calibration, fuzz.
4. Pick reference clips from D1 (one harmonic stack, one high note) and render the listening page.

## 6. Out of scope

Pitch tracking of the OEC (C3), beak/OEC gestures (C-phase), tongue, source–tract feedback.

## 7. Tests & acceptance criteria

Internal rate 192 kHz unless stated.

| # | Criterion |
|---|---|
| 1 | Equivalence (Python, analytic): published state equations (research §4.7.1) and the circuit transfer function agree to < 1e-9 relative magnitude and < 1e-9 rad phase at 20 frequencies, 5 random parameter sets. |
| 2 | Discrete flow response (impulse response → FFT) matches the analytic $H_{uvt}(j\omega)$ within 0.2 dB from 50 Hz to 6 kHz and within 0.5 dB from 6 to 12 kHz (trapezoidal frequency warping) for 6 parameter sets (f_H ∈ {1000, 3000, 6000} × gape ∈ {0.2, 0.9}). |
| 3 | Radiated response peak frequency within ±5 % of `oec.freq_hz` for f_H ∈ {1000, 2000, 3000, 4500, 6000, 8000} × gape ∈ {0.2, 0.5, 0.9} at default dampings; table of peak frequency and −3 dB Q produced (report). |
| 4 | Stability fuzz: 10 s noise input; f_H modulated 1–10 kHz by a 20 Hz LFO, gape 0–1 by a 7 Hz LFO, dampings random in range: no non-finite values, peak output < 100× the static-parameter peak. |
| 5 | Calibration: reference note peak at host rate = −6 dBFS ± 1 dB (ρ = 1); ρ = 0 RMS within ±3 dB of ρ = 1. |
| 6 | Parity C++ vs numpy reference (0.1 s, 3 settings): max abs difference < 1e-9 relative. |
| 7 | CPU: full voice (source + trachea + UVT + decimation) renders 10 s at 48 kHz in < 0.2 s single-threaded Release on the dev Mac (report the number). |
| 8 | Zero allocations in `process()`; `scripts/check.sh` passes. |

## 8. Listening & results — milestone M1

Render into `listening/B3/` (48 kHz). Build groups so that only one thing changes at a time.

| File | Content | Listen for |
|---|---|---|
| `01_chain_source.wav`, `02_chain_trachea.wav`, `03_chain_full.wav` | same rich source (~1.5 kHz) through: nothing / trachea / trachea + UVT; group "chain" | how each anatomical stage shapes the sound; 03 should be the first sound that reads as "a bird" |
| `04_oec_sweep.wav` | rich low source (choose β ≈ 0.05 and α just above onset so f₀ ≈ 600 Hz; report values), f_H 800 → 6000 Hz over 5 s | a formant moving through the harmonics; individual harmonics light up |
| `05_gape_sweep.wav` | same source, gape 0 → 1 over 4 s at f_H = 3000 Hz | brightness/openness change |
| `06_flow_vs_radiation.wav` | 2 s ρ = 0 then 2 s ρ = 1 | radiation = brighter, more present |
| `07_stack_attempt.wav` vs reference real zebra finch harmonic stack (D1 clip) | f₀ ≈ real clip's f₀, f_H near its strongest harmonic, hand-tuned | first comparison with the real bird: how far are we? (no fitting yet) |
| `08_high_note_attempt.wav` vs reference real high note (D1 clip) | tonal source at the clip's f₀ | whistle quality vs real |
| `09_tonal_glide_full.wav` | β sweep (as B1 item 03) through the full chain | resonance interactions during a glide |

Figures: radiated response family (peak table), chain spectra overlay for items 01–03.

Final message: this is **milestone M1**; invite the human to spend time listening, and ask for their impression of items 07/08 (what is most different from the real bird: noise, attack, harmonic balance, pitch movement?). Their answer steers B4.

## 9. Pitfalls

- Do not differentiate the flow numerically for radiation; use the state formula (exact and noise-free).
- With very small gape the beak inertance is large; the resonance still follows f_H by construction (C_h adapts), but implied volumes become large — this is fine for control, report the numbers.
- Keep the matrix update out of the per-sample path when parameters are static.

## Notes from WP-B2 (2026-09-16)

- Chain today: labial `x` → `Trachea` → 20 Hz DC blocker → `×0.5` → half-band decimation → `out.gain_db`. Put the UVT **between trachea and DC blocker** (r < 0 still needs the DC block after the tube).
- `Trachea` is `core/include/cuicui/model/trachea.hpp`. `prepare(rate, maxLengthMm=480)` allocates; `setParams` is immediate (voice already smooths length/r/loss/A at 20 ms). Min delay D is clamped to 3 samples.
- ℓ = 0 bypasses the one-pole (true lossless comb). ℓ > 0 uses f_c = 22000·(3000/22000)^ℓ. A 22 kHz pole at 192 kHz is not transparent enough for the 0.3 dB analytic test.
- `BirdVoice::setSource(Labial)` enables the trachea; `setTracheaEnabled(false)` for dry source (B3 item 01). Test oscillator stays dry unless a render spec mentions `trachea.*`.
- Python: `cuicui_core.trachea_process(x, rate, length_mm, r, loss, A)` and `render_labial(..., use_trachea=True, trachea_length_mm=33, ...)`.
- Default `trachea.reflection` is still +0.5 (D-012); B2 asks the human which sign they prefer from items 01–03.
- D1 is still `optional`.
