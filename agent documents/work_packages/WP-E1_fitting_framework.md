# WP-E1 — Fitting framework, losses, identifiability benchmark

| Field | Value |
|---|---|
| Phase | E — Fitting (regression) |
| Depends on | D2, B6, C2, C4 |
| Size | M |
| Milestone | – |

## 1. Goal

Build the infrastructure to fit time-varying controls of the **real C++ engine** to a target sound: control parametrisations (B-splines, bounded transforms, pitch locking), losses (multi-resolution spectral, pitch, envelope, harmonics, entropy, smoothness), a parallel resumable CMA-ES optimiser, fit-result files, A/B listening pages, and — most importantly — an **identifiability benchmark** of synthetic syllables with known ground truth. A simple baseline fit establishes reference numbers that E2–E4 must beat.

## 2. Read first

- `01_research.md` **§8 (all)**, §7.4 (inverse map), §9
- `02_architecture.md` §6.1 (bindings), §9.4, §9.6, §5.2
- Reports of WP-D2 (features API), WP-B6 (`render_controls`, state), WP-C2 (`inverse_solve`), WP-C4 (`render_score`)

## 3. Deliverables

| Path | Content |
|---|---|
| `python/cuicui/fit/__init__.py` | package |
| `python/cuicui/fit/controls.py` | control parametrisation (splines, transforms, pitch locking) → physio tracks |
| `python/cuicui/fit/engine.py` | rendering wrapper (per-process `cuicui_core.Voice`, state handling, direct-α tracks) |
| `python/cuicui/fit/losses.py` | loss terms and weighted total |
| `python/cuicui/fit/optim.py` | CMA-ES wrapper (pycma), parallel evaluation, checkpoint/resume, logging |
| `python/cuicui/fit/benchmark.py` | synthetic identifiability benchmark generation + evaluation |
| `python/cuicui/fit/baseline.py` | baseline whole-syllable fit |
| `python/cuicui/fit/report.py` | A/B listening pages, metrics tables |
| `python/cuicui/fit/__main__.py` | CLI |
| `python/bindings/module.cpp` | `render_controls` and the score player accept optional fitting-only tracks `src.L.alpha_direct` / `src.R.alpha_direct`; `render_score(..., record_controls=True)` returns the physio tracks it produced |
| `data/benchmarks/fit_v1/` | benchmark set (NOT committed; generation is deterministic and scripted) |
| `python/tests/test_fit_*.py` | tests |

CLI:
```bash
uv run python -m cuicui.fit benchmark-make --out data/benchmarks/fit_v1 --n 40 --seed 1
uv run python -m cuicui.fit baseline --bench data/benchmarks/fit_v1 --jobs 8
uv run python -m cuicui.fit baseline --dataset zf_v1 --labels all --per-label 3 --jobs 8
```

## 4. Specification

### 4.1 Fitting control vector (single side, per syllable)

Time-varying controls on a common time axis (1 ms control rate):

| Control | Range / transform | Physiological meaning |
|---|---|---|
| `p(t)` | [0, 1] via logistic | `resp.pressure` (drives G, noise) |
| `tone(t)` | [0, 1] via logistic | α(t) = α_lo(ν) + tone·(α_hi(ν) − α_lo(ν)) from the inverse map |
| `delta_cents(t)` | [−100, 100] via tanh | pitch-locking correction |
| `oec_hz(t)` | [300, 10000] via log | `oec.freq_hz` |
| `gape(t)` | [0, 1] via logistic | `beak.gape` |

- **Pitch locking:** $\nu(t) = 2\pi f_{0}^{rec}(t)\,2^{\delta(t)/1200}/\gamma$ with $f_0^{rec}$ the target's smoothed f₀ track (D2); $\beta(t) = B(\alpha(t), \nu(t))$ via `inverse_solve`. Unvoiced frames: f₀ interpolated from neighbours; silent frames: p → 0 allowed.
- **Direct α:** to avoid respiratory-dynamics coupling during fitting, renders use the track `src.L.alpha_direct` = α(t) (overrides α_base(p) + offset inside the voice). Pressure still drives G and noise. These `*.alpha_direct` IDs are **fitting-only track IDs**, not host parameters: accepted by `render_controls` and in gesture-score `physio.tracks`, rejected elsewhere. (Add a `proposed` decision entry for this contract addition.)
- **Splines:** each control is a clamped cubic B-spline in the transformed (unbounded) space; knot spacing default 10 ms (baseline), configurable; coefficients form the optimisation vector.
- Per-clip constants (fixed in E1): bird settings from defaults; output gain fitted analytically per evaluation (least-squares scalar on magnitude spectrograms) so the optimiser does not waste dimensions on level.

### 4.2 Losses (all on magnitude features; target and render analysed by the same D2 code)

- **MRSTFT:** for FFT sizes {256, 512, 1024, 2048} × (sr/44100) rounded to powers of two, hop 25 %, Hann: spectral convergence $\|\,|S| - |\hat S|\,\|_F / \|\,|S|\,\|_F$ + log-magnitude L1 $\frac{1}{N}\|\log(|S|+\epsilon) - \log(|\hat S|+\epsilon)\|_1$; averaged over resolutions; 300 Hz – 12 kHz only.
- **Pitch:** mean squared cents error on frames voiced in both (Huber, δ = 50 cents) + penalty for voicing mismatch.
- **Envelope:** mean squared dB error of `rms_db` (frames above target noise floor).
- **Harmonics:** mean squared dB error of h1..h8 (clipped at −40 dB) on voiced frames.
- **Entropy:** mean squared error of Wiener entropy.
- **Smoothness:** sum of squared second differences of spline coefficients (per control, scaled).
- **Total:** weighted sum; weights in a YAML/JSON config `python/cuicui/fit/configs/default.json`; each term normalised by its median value on the benchmark's *initial* guesses so defaults are balanced. Report the per-term breakdown in every fit log.

### 4.3 Optimiser

- pycma `CMAEvolutionStrategy` on the coefficient vector; population default 4 + 3 ln(n); σ₀ in transformed units (default 0.5); max evaluations configurable; stop on tolerance.
- Parallel evaluation with `multiprocessing` (spawn), one `cuicui_core` voice per worker; the GIL is released in rendering anyway.
- Checkpoint every N generations (pickle of CMA state + best), resume automatically.
- Fit log JSON: config, git hash, seeds, per-generation best loss and term breakdown, evaluations, wall time.

### 4.4 Identifiability benchmark

- 40 synthetic syllables (seeded): 25 from C4 archetype scores (all archetypes represented; durations 50–300 ms; f₀ 500–4000 Hz; tones 0.05–1), 15 from random smooth physio trajectories within the fitting control ranges.
- Ground truth: physio tracks recorded from the score player (`record_controls=True`) + the fitting-control representation (tone, oec, gape, p).
- Degradations: each syllable in 3 versions — clean, + white noise at 30 dB SNR, + noise 20 dB & short synthetic reverb (exponentially decaying noise IR, RT60 60 ms).
- Metrics per fit: total and per-term loss; f₀ error (cents, median/p90); control-curve RMSE (tone, oec in cents, gape, p); time per fit.

### 4.5 Baseline fit

- Whole-syllable CMA-ES, knots 10 ms, from a naive initial guess (tone 0.5, oec 3000 Hz, gape 0.5, p from normalised target envelope, δ = 0), ≤ 3000 evaluations.
- Run on the benchmark and on 3 examples per label of `zf_v1`.

## 5. Suggested steps

1. Bindings extension (direct α, record controls) + tests.
2. Controls parametrisation + tests (round trip, bounds, pitch locking gives target f₀ in clean renders).
3. Losses + tests (identical → 0; monotone under increasing perturbation; gain invariance).
4. Optimiser with parallelism and resume.
5. Benchmark generation; baseline fits; report pages.

## 6. Out of scope

Atlas-based initialisation (E2), windowed refinement (E3), bird constants (E4), two-voice fits (E4).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Pitch locking sanity: rendering the benchmark's clean syllables with *ground-truth* tone/oec/gape/p and δ = 0 gives f₀ median error ≤ 5 cents vs the ground-truth render (validates the inverse-map path). |
| 2 | Loss sanity: loss(target, target) < 1e-6; loss increases monotonically for gain-free perturbations of increasing size (tone offsets 0.05, 0.1, 0.2) on 5 syllables. |
| 3 | Determinism: same config + seeds → identical best coefficients and loss. |
| 4 | Resume: killing a fit mid-run and restarting continues from the checkpoint and reaches the same result as an uninterrupted run (same seeds). |
| 5 | Parallel efficiency ≥ 0.6 × number of physical cores (report). |
| 6 | Baseline on benchmark: report median/p90 of total loss, f₀ error, control RMSE for clean/30 dB/20 dB+reverb. (No fixed threshold; these become the reference numbers in `state.md` "Baselines".) |
| 7 | Baseline on real data (3 per label) completes; A/B page generated. |
| 8 | `scripts/check.sh` passes. |

## 8. Listening & results

- `listening/E1/bench/`: for 10 benchmark syllables: target (degraded) vs baseline fit vs ground-truth clean, spectrograms with f₀ overlays; control curves (fit vs truth) figures.
- `listening/E1/real/`: per label, 3 real syllables with baseline resynthesis (A/B via `reference`).
- Report: benchmark metrics table; loss-term breakdown; runtime.

Tell the human plainly that the baseline is expected to be mediocre on real data — its purpose is to create the yardstick.

## 9. Pitfalls

- Analyse target and render with **identical** feature settings (sample rate, windows); resample renders to the target's rate if needed.
- Onsets/offsets: time alignment between target and render must be exact (clip padding from D1); do not let the optimiser shift time.
- CMA-ES in > 200 dimensions is slow — the baseline is allowed to be slow; E3's windowing solves it.
