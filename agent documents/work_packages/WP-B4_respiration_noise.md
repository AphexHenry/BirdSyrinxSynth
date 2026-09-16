# WP-B4 — Respiratory dynamics & noise sources

| Field | Value |
|---|---|
| Phase | B — The bird body |
| Depends on | B3 |
| Size | M |
| Milestone | – |

## 1. Goal

Make the voice breathe and roughen: add **air-sac pressure dynamics** (so onsets and offsets are physical and articulated), **flow-dependent source gain**, **tension noise** (natural irregularity, buzzy notes near threshold) and **aspiration/turbulence noise**. All noise is seedable and reproducible.

## 2. Read first

- `01_research.md` §2.2, **§4.2**, **§4.5**, §4.3.2 (regimes), §6.4 (determinism)
- `02_architecture.md` §6, §7.1
- Reports of WP-B3 (especially the human's impressions of the M1 comparison) and WP-D1 (observations on noisy notes)

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/model/respiratory_drive.hpp` | `RespiratoryDrive` |
| `core/include/cuicui/model/noise_sources.hpp` | `TensionNoise` (per side), `AspirationNoise` |
| `core/include/cuicui/dsp/biquad.hpp` | RBJ band-pass biquad (if not present) |
| `core/include/cuicui/dsp/rng.hpp` | add `splitmix64` seed derivation for independent streams |
| `core/include/cuicui/model/bird_voice.hpp` | pressure dynamics → α and G; noise injected |
| `params.def` | add `resp.attack_ms`, `resp.release_ms`, `noise.beta_sigma`, `noise.beta_bandwidth_hz`, `noise.aspiration`, `engine.seed` (if not present) |
| `core/tests/test_respiration_noise.cpp` | tests §7 |

## 4. Specification

### 4.1 Respiratory drive

- Input u = `resp.pressure` (already ramped per internal sample). State p.
- Exact one-pole update per internal sample: $p \leftarrow u + (p - u)\,e^{-h/\tau}$, τ = attack if u > p else release (ms → s).
- $\alpha_{base} = -0.10 + 0.55\,p$; $G = \sqrt{\max(p, 0)}$.
- The source signal before the trachea becomes $G\cdot\tilde x_L$ (+ aspiration). **Re-calibrate** the output constants so the B3 reference note (now: u such that steady α = 0.2, i.e. p ≈ 0.545) still peaks at −6 dBFS ± 1 dB.

### 4.2 Tension noise

- Per side, independent stream: white Gaussian $w[n]$ (unit variance) → one-pole low-pass with cutoff $f_\eta$ = `noise.beta_bandwidth_hz` → rescale to unit variance: multiply the input by $\sqrt{(1+a)/(1-a)}$ where $a = e^{-2\pi f_\eta/f_{int}}$.
- $\beta_{eff} = \beta + \sigma_\beta\,\eta$, σ_β = `noise.beta_sigma`. Clamp β_eff to [−0.5, 1.5].

### 4.3 Aspiration noise

- Independent white Gaussian stream → RBJ band-pass (centre 4 kHz, Q = 0.7) at internal rate.
- $n_{asp} = a_{asp}\,G\,\big(0.5 + 0.5\tanh(4\,\bar x)\big)\cdot \mathrm{BP}\{w\}$, $a_{asp}$ = `noise.aspiration`, $\bar x$ = DC-blocked displacement of the open side(s) (only L exists until B5).
- Added to the source signal before the trachea.

### 4.4 Seeding

- `engine.seed` → `splitmix64` → independent seeds for: tension noise L, tension noise R (reserved for B5), aspiration. Adding the R stream later must **not** change the L and aspiration streams.

## 5. Suggested steps

1. `RespiratoryDrive` + tests; voice integration; re-calibration.
2. Noise generators + statistical tests.
3. Seeding and determinism tests.
4. Explore σ_β near threshold (β ≈ 0–0.1) to find buzzy settings; listening renders.

## 6. Out of scope

Second side (B5), size scaling of time constants (B6), mini-breath *patterns* (they are generated in C4/C5 from gestures; here only demonstrated by automation).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Pressure step 0 → 1: reaches 63.2 % at τ_att ± 2 %; step 1 → 0 reaches 36.8 % at τ_rel ± 2 % (τ ∈ {2, 8, 50} ms). |
| 2 | Tension noise η: variance 1 ± 5 % (10 s), −3 dB bandwidth of its PSD within ±10 % of f_η ∈ {100, 300, 1000} Hz. |
| 3 | σ_β = 0.02 at (α, β) = (0.2, 0.5): mean f₀ within 1 % of the noiseless value; cycle-to-cycle period jitter (std/mean) > 5× the noiseless value. Report the jitter numbers. |
| 4 | Aspiration generator tested directly (unit test on `AspirationNoise` with a fixed $\bar x$ = 0): output RMS for p = 0.25 divided by RMS for p = 1 equals 0.5 ± 10 % (√p law); output exactly zero for p = 0. |
| 5 | Determinism: same seed → bit-identical renders; different seed → different renders; the L tension-noise stream is unchanged when the (future) R stream is drawn (test by drawing extra R values in between). |
| 6 | Onset latency (report, no pass/fail): pressure command step 0 → 0.6 with τ_att = 8 ms: time until output envelope reaches −20 dB of steady state, for β = 0.05 and β = 0.5. |
| 7 | Fuzz 30 s with all parameters randomly modulated in range: no non-finite values. |
| 8 | Zero allocations in `process()`; `scripts/check.sh` passes. |

## 8. Listening & results

Render into `listening/B4/` (48 kHz, full chain).

| File | Content | Listen for |
|---|---|---|
| `01_attack_2_8_30ms.wav` | three identical notes with τ_att = 2, 8, 30 ms (β = 0.2) | from sharp "tick" onset to soft swell |
| `02_syllable_train.wav` | pressure pattern: 6 short syllables (60 ms) separated by 40 ms mini-breaths, slight pitch variation via β | articulated phrasing; offsets should not click |
| `03_buzz_sigma_sweep.wav` | β ≈ 0.02, α just above onset, σ_β 0 → 0.05 over 4 s | clean reedy tone becomes rough, intermittent, buzzy |
| `04_aspiration_sweep.wav` | tonal note, aspiration 0 → 0.5 over 4 s | breath/air noise added, pulsed with the tone (not a separate hiss layer) |
| `05_jitter_ab.wav` | 2 s σ_β = 0, 2 s σ_β = 0.01 at a tonal note | subtle organic instability in the second half |
| `06_noisy_note_attempt.wav` vs reference real noisy zebra finch note (D1) | hand-tuned buzz | how close is the texture? |
| `07_stack_attempt_v2.wav` vs same reference as B3 item 07 | B3 stack with tension noise + aspiration tuned by ear | improvement over M1? |

Figures: pressure step responses; f₀ track with/without tension noise; spectrograms of 03 and 06 vs reference.

## 9. Pitfalls

- Noise on β near the onset boundary can push the oscillator *below* threshold (intermittent silence) — that is expected and musically useful; don't "fix" it by clamping α.
- Don't use `std::normal_distribution` (implementation-defined → not reproducible across platforms); implement Box–Muller or Ziggurat on top of xoshiro.
- Re-calibration changes loudness of all earlier golden renders: update expected values and mention it.

## Notes from WP-B3 (2026-09-17)

- Output mix is already calibrated (`kUvtFlowCal`, `kUvtRadCal` in `upper_vocal_tract.hpp`). Keep them unless you re-run the B3 reference note (α=0.2, β=0.2, trachea defaults, f_H=3000, gape=0.5) at 48 kHz.
- Labial defaults to trachea **and** UVT on. `setUvtEnabled(false)` restores the B2 dry-UVT path (`0.5 * DC(trachea)`).
- For a low stack near 540 Hz use α≈0.12, β=0.05 — α=0.05 is still silent at that β.
- Same real clips as B3 07/08 if you want a v2 comparison (`22938.wav` 7.828–7.994 s peep and 23.48–23.98 s alarm).
- **Human on B3 item 08 (alarm):** the real note is more **metallic**, like **FM with fast modulation**. That is the B4 target for `noise.beta_sigma` / tension jitter (not the UVT). Item 06 in this WP should chase that texture.

