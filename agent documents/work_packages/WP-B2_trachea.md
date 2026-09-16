# WP-B2 — Trachea waveguide

| Field | Value |
|---|---|
| Phase | B — The bird body |
| Depends on | B1 |
| Size | M |
| Milestone | – |

## 1. Goal

Add the **trachea**: a tube between syrinx and glottis that adds resonances to the source. Implement it as a delayed-feedback waveguide with fractional delay, frequency-dependent loss and a reflection coefficient whose sign decides between quarter-wave and half-wave resonance patterns. Let the human hear the difference and choose a default sign.

## 2. Read first

- `01_research.md` §2.4, **§4.6**, §4.9 (size scaling, only to prepare for it), §6
- `02_architecture.md` §6, §7.1, §10
- Report of WP-B1

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/dsp/lagrange.hpp` | 3rd-order Lagrange fractional delay read from a circular buffer |
| `core/include/cuicui/dsp/one_pole.hpp` | one-pole low-pass (if not already present) |
| `core/include/cuicui/model/trachea.hpp` | `Trachea` |
| `core/include/cuicui/model/bird_voice.hpp` | chain: source → trachea → DC block → gain → decimation |
| `params.def` | add `trachea.length_mm`, `trachea.reflection`, `trachea.loss`, `trachea.input_gain` |
| `core/tests/test_trachea.cpp` | tests §7 |
| `python/cuicui/reference/trachea.py` | numpy reference |
| `python/cuicui/analysis/trachea_response.py` | measured vs analytic magnitude responses (plots) |

## 4. Specification

### 4.1 Equations

$$
p_i[n] = A\,s[n] - r\,\mathrm{LP}\{p_i\}[n - D], \qquad p_t[n] = (1 - r)\,p_i[n - D/2]
$$

- $D = \tau f_{int} = \dfrac{2L}{c}f_{int}$ (fractional), $L$ = `trachea.length_mm`/1000, $c$ = 350 m/s, $A$ = `trachea.input_gain`, $r$ = `trachea.reflection`.
- LP: one-pole low-pass $y[n] = (1-a)\,u[n] + a\,y[n-1]$, $a = e^{-2\pi f_c/f_{int}}$, cutoff $f_c = 22000\cdot(3000/22000)^{\ell}$ with $\ell$ = `trachea.loss` ∈ [0, 1]. (ℓ = 0 → almost lossless.)
- Both delayed reads use 3rd-order Lagrange interpolation on the same circular buffer of $p_i$.
- Analytic response for ℓ = 0 (for tests): $|H_{tr}(f)| = \dfrac{|A(1-r)|}{|1 + r\,e^{-j2\pi f\tau}|}$.

### 4.2 Class

```cpp
namespace cuicui::model {
class Trachea {
public:
    void prepare(double internalRate, double maxLengthMm) noexcept(false); // allocates buffer
    void reset() noexcept;
    void setParams(double lengthMm, double reflection, double loss, double inputGain) noexcept; // smoothed by caller
    double process(double source) noexcept;  // returns p_t
};
}
```

- Buffer size ≥ `ceil(2·maxLength/c·f_int) + 4`. `maxLengthMm` = 120·4 (to allow `body.size` up to 4 later).
- Minimum delay D ≥ 3 samples (clamp length accordingly and document).
- Parameter changes are smoothed by the voice (registry smoothing), so length modulation is slow; Lagrange interpolation must stay click-free under that modulation.

## 5. Suggested steps

1. Lagrange delay + tests (group delay accuracy on sines).
2. Trachea class + analytic response tests.
3. Voice integration; Python reference + parity; response plots.
4. Listening renders.

## 6. Out of scope

Glottis/OEC/beak (B3), two-rail waveguide with junction scattering (not needed), tracheal length modulation during song (research §2.4: negligible).

## 7. Tests & acceptance criteria

Internal rate 192 kHz unless stated.

| # | Criterion |
|---|---|
| 1 | Lagrange delay: for delays 3.0–40.0 samples in 0.25 steps, a 1 kHz sine is delayed with phase error < 0.1° and amplitude error < 0.01 dB; at 10 kHz amplitude error < 0.5 dB. |
| 2 | Resonances (ℓ = 0, A = 1, L = 33 mm): r = +0.5 → magnitude peaks at 2651, 7955, 13258 Hz (±0.5 %); r = −0.5 → peaks at 5303 and 10606 Hz (±0.5 %) plus a low-frequency maximum. |
| 3 | Measured magnitude response (impulse response, 1 s, FFT) matches the analytic formula within 0.3 dB from 50 Hz to 12 kHz for r ∈ {−0.9, −0.5, 0.5, 0.9}, L ∈ {20, 33, 80} mm. |
| 4 | Loss: with ℓ = 1, the peak at ≈ 8 kHz (r = 0.5, L = 33 mm) is at least 6 dB lower than with ℓ = 0. |
| 5 | Stability: 10 s white noise input with random smooth trajectories of r ∈ [−0.95, 0.95], ℓ ∈ [0,1], L ∈ [10, 120] mm: output finite and bounded (peak < 40× input peak). |
| 6 | Modulation smoothness: 500 Hz sine input, L swept 20 → 60 mm over 1 s (with registry smoothing): energy above 15 kHz stays > 60 dB below the 500 Hz component. |
| 7 | Parity C++ vs numpy reference (0.1 s noise input, 3 settings): max abs difference < 1e-9 relative to signal peak. |
| 8 | Zero allocations in `process()`; `scripts/check.sh` passes. |

## 8. Listening & results

Render into `listening/B2/` (48 kHz). Use the same source in A/B groups so only the trachea changes.

| File | Content | Listen for |
|---|---|---|
| `01_src_rich_raw.wav` / `02_src_rich_trachea_r+05.wav` / `03_src_rich_trachea_r-05.wav` | rich source (β = 0.05, α ≈ 0.18, ~1.5 kHz), 2 s each; group "reflection sign" | r > 0: energy around 2.6 and 8 kHz (quarter-wave); r < 0: around 5.3 kHz (half-wave). Which sounds more "bird-like" to you? |
| `04_src_tonal_trachea_r+09.wav` | tonal source β-sweep (as B1 item 03) through r = 0.9 | loudness bumps when the pitch crosses a tube resonance |
| `05_length_sweep.wav` | rich source, L 15 → 80 mm over 4 s, r = 0.5 | resonances glide down; no clicks |
| `06_loss_0_vs_1.wav` | 2 s ℓ = 0 then 2 s ℓ = 1 | the second half is darker/softer at high frequencies |
| `07_noise_through_trachea.wav` | white noise through r = 0.9, L = 33 mm | clearly pitched "tube" colour |

Figures: measured vs analytic magnitude responses for r = ±0.5 and ±0.9.

In the final message, **ask the human which reflection sign they prefer** as default (research D-012), with items 01–03 as the basis.

## 9. Pitfalls

- The output tap is at D/2, the feedback tap at D — both fractional.
- r < 0 creates a DC/low-frequency peak: the voice's DC blocker must come after the trachea.
- Don't smooth the delay length inside `Trachea` again if the voice already smooths the parameter (double smoothing = sluggish size changes later).

## Notes from WP-B1 (2026-09-16)

- `BirdVoice` has `VoiceSource::Labial` and `::Test`. Offline render uses labial when the spec mentions `resp.pressure`, `src.L.beta`, `src.L.alpha_offset`, or `src.gamma`.
- Output today: `0.5 * DcBlocker_20Hz(x)` at internal rate, then half-band decimation, then `out.gain_db`. Put the trachea **before** the DC blocker if r < 0 (this WP's pitfall).
- `LabialOscillator` is `core/include/cuicui/model/labial_oscillator.hpp`. Prime from rest: `resetToRest(-0.1, beta)`.
- D1 is parked (`optional`); do not auto-start it.
