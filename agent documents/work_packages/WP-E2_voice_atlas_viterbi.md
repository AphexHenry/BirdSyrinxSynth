# WP-E2 — Voice atlas + Viterbi initial path

| Field | Value |
|---|---|
| Phase | E — Fitting (regression) |
| Depends on | E1, C2 |
| Size | M |
| Milestone | – |

## 1. Goal

Give the fitting a smart starting point. Precompute a **voice atlas**: steady-state spectral features of the *full* voice over a grid of pitch, tone, OEC resonance and beak gape. For each frame of a target syllable, pick candidate settings from the atlas that match its spectrum at its pitch, and find the smoothest good path through time with **Viterbi** (dynamic programming). The result is an initial control trajectory θ₀(t) that already sounds close and that E3 refines.

## 2. Read first

- `01_research.md` **§8.4 (Stages 2–3)**, §8.2
- Reports of WP-E1 (controls, losses, benchmark numbers), WP-D2 (features), WP-C2 (inverse map, playable range)

## 3. Deliverables

| Path | Content |
|---|---|
| `python/cuicui/fit/voice_atlas.py` | atlas generation (parallel, resumable, cached by settings hash) |
| `python/cuicui/fit/viterbi.py` | candidate selection + Viterbi decoding → θ₀ |
| `python/cuicui/fit/init_fit.py` | pipeline: target → features → θ₀ → optional short local CMA-ES polish → fit result |
| `data/atlas/voice_<hash>.npz` | voice atlas (NOT committed; reproducible) |
| `python/tests/test_viterbi.py` | tests |

CLI:
```bash
uv run python -m cuicui.fit atlas-make --settings defaults --jobs 8
uv run python -m cuicui.fit init --bench data/benchmarks/fit_v1 --jobs 8
uv run python -m cuicui.fit init --dataset zf_v1 --labels all --jobs 8
```

## 4. Specification

### 4.1 Voice atlas

- Settings = bird constants (defaults now; E4 regenerates with fitted constants). Cache key = hash of settings + atlas grid + engine git hash.
- Grid:
  - ν: 128 log-spaced points covering the playable range at σ = 1 (from C2),
  - tone: 12 points in [0.02, 1],
  - f_H: 24 log-spaced points 600 Hz – 10 kHz,
  - gape: {0.2, 0.5, 0.8}.
  → 110 592 cells.
- Each cell: render 80 ms steady state (single side L, pressure p = 0.7 constant, direct α from the inverse map, noise off, fixed seed), analyse the last 40 ms with D2: h1..h16 (relative dB), env_00..env_31, wiener_entropy, sci, rms_db, goodness_of_pitch. Store as float32 arrays indexed [ν, tone, f_H, gape, feature].
- Parallel over ν rows; resumable; report generation time and file size.

### 4.2 Emission cost

For a target frame n with features $F^{rec}_n$ and pitch $\nu_n$:
- interpolate atlas features linearly in log ν between the two nearest ν rows → $F^{atl}(c)$ for every state $c$ = (tone, f_H, gape) (864 states),
- $e_n(c) = w_h\,\mathrm{Huber}(h^{rec}_{1..8} - h^{atl}_{1..8}) + w_{env}\,\mathrm{Huber}(env^{rec} - env^{atl}) + w_W\,(W^{rec} - W^{atl})^2$ (weights in config; features clipped at −40 dB).
- Keep the K = 64 cheapest states per frame.

### 4.3 Transition cost and decoding

- $t(c, c') = w_T\,(\text{tone} - \text{tone}')^2 + w_H\,(\log_2 f_H - \log_2 f_H')^2 + w_g\,(g - g')^2$.
- Viterbi over the pruned candidate sets (K × K per frame); frames every 1 ms (optionally decode at 2 ms and upsample).
- **Silent frames** (target RMS below noise floor + 6 dB): special state "silent" (p = 0; other controls carried over); transitions into/out of silence cost 0 for the other controls.
- **Unvoiced but loud frames** (high entropy, no reliable f₀): use the last reliable ν and restrict candidates to tone ≤ 0.2 (buzz hypothesis); flag these frames in the result.
- Output θ₀: tone(t), oec(t), gape(t) (smoothed with a 5 ms median filter), δ(t) = 0, p(t) from the target envelope via a per-state RMS lookup (atlas `rms_db` at the chosen state) solved per frame, then smoothed.
- Convert θ₀ to spline coefficients (least squares) in E1's parametrisation.

### 4.4 Optional polish

- Short CMA-ES (≤ 300 evaluations) on the whole syllable from θ₀ with E1 losses. Report results with and without polish.

## 5. Suggested steps

1. Voice atlas generation (start with a reduced grid to validate, then full).
2. Emission/transition costs; Viterbi with unit tests on toy problems (known best path).
3. θ₀ construction and conversion to E1 controls.
4. Benchmark evaluation vs E1 baseline; real data run; listening pages.

## 6. Out of scope

Windowed refinement (E3), bird constants (E4), two-voice decoding (E4).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Viterbi unit tests: on synthetic cost tables with a planted optimal path, the decoder returns it exactly; runtime scales linearly with frames. |
| 2 | Atlas self-consistency: for 200 random cells, re-rendering and analysing gives features within 0.5 dB (h, env) of stored values. |
| 3 | Benchmark (clean): median control RMSE of θ₀ ≤ 60 % of the E1 baseline's; median total loss of θ₀ (no polish) ≤ E1 baseline median. |
| 4 | Benchmark (20 dB + reverb): θ₀ total loss ≤ 1.1 × E1 baseline median (robustness). |
| 5 | Real data (all syllables of `zf_v1`, or a documented subset if runtime > 12 h): θ₀ loss better than the E1 naive initial guess on ≥ 90 % of syllables. |
| 6 | Time per syllable (θ₀ without polish) ≤ 10 s on the dev Mac (report distribution). |
| 7 | `scripts/check.sh` passes. |

## 8. Listening & results

- `listening/E2/`: per label, 3 real syllables: target vs θ₀ vs θ₀+polish (group), spectrograms with f₀; control curve figures.
- Benchmark table: E1 baseline vs θ₀ vs θ₀+polish.
- A figure showing, for one syllable, the emission cost heat map over time (best cost per tone) with the decoded path.

## 9. Pitfalls

- Atlas renders must use the same sample rate and analysis settings as the targets (or resample consistently).
- Harmonic features above Nyquist or below the noise floor must be masked, not zero-filled.
- Don't let silence handling create hard jumps in tone/oec that later cost smoothness penalties in E3.
