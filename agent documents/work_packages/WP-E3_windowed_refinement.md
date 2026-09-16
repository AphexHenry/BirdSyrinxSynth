# WP-E3 — Windowed CMA-ES refinement → gesture scores

| Field | Value |
|---|---|
| Phase | E — Fitting (regression) |
| Depends on | E2 |
| Size | L |
| Milestone | – |

## 1. Goal

Turn the rough initial path into accurate frame-by-frame controls by optimising the **full dynamics** of the real engine window by window, carrying the voice state across windows, then polishing the whole syllable. Produce a **gesture score** (physiological tracks) for every annotated syllable and A/B listening pages against the real recordings. Compute time is not a constraint; robustness and resumability are.

## 2. Read first

- `01_research.md` **§8.4 (Stage 4)**, §8.2, §9
- `02_architecture.md` §9.4, §9.6
- Reports of WP-E1 and WP-E2 (numbers to beat, configs)

## 3. Deliverables

| Path | Content |
|---|---|
| `python/cuicui/fit/refine.py` | windowed refinement |
| `python/cuicui/fit/run.py` | dataset-level runner (queue, parallel across syllables, resume, summary) |
| `python/cuicui/fit/configs/refine_default.json` | weights, windows, CMA-ES settings |
| `data/fits/<run_id>/<segment_id>.gscore.json` + `.fitlog.json` | results (NOT committed) |
| `data/fits/<run_id>/summary.parquet` | per-syllable metrics |
| `agent documents/reports/assets/E3/` | key figures |
| `python/tests/test_refine.py` | tests on short synthetic cases |

CLI:
```bash
uv run python -m cuicui.fit refine --bench data/benchmarks/fit_v1 --run bench_e3 --jobs 8
uv run python -m cuicui.fit refine --dataset zf_v1 --run zf_e3 --jobs 8 [--labels a,b] [--resume]
uv run python -m cuicui.fit summary --run zf_e3      # → listening/E3/ + tables
```

## 4. Specification

### 4.1 Parametrisation

- E1 controls (p, tone, δ, oec, gape) as cubic B-splines with **knot spacing 5 ms**, 2.5 ms within ±15 ms of detected onsets/offsets (D2 envelope).
- Initialisation: least-squares projection of E2's θ₀ onto these splines.

### 4.2 Windowed optimisation (left to right)

- Windows of 40 ms, hop 20 ms, covering the syllable plus 10 ms of padding on each side.
- For window w: the **free coefficients** are those whose spline support intersects the central 20 ms of the window; all others are fixed at their current values.
- **State carry:** render the current solution from the syllable start up to the window start once, `save_state()`; every candidate evaluation `load_state()` and renders only [window start, window end + 10 ms]. (B6 state API; bit-exact.)
- Loss on the window region (E1 terms; smoothness only on free coefficients and their neighbours).
- CMA-ES: population 24, ≤ 200 generations, σ₀ derived from E2 uncertainty (spread of the top-5 Viterbi states around the chosen path, mapped to transformed units; floor 0.1), bounds via transforms; stop when the relative improvement over 20 generations < 0.1 %.
- After each window, accept the new coefficients only if the window loss did not increase (guard against divergence).

### 4.3 Global polish

- Second pass: whole-syllable CMA-ES on **all** coefficients with small σ (0.05), population 32, ≤ 150 generations, full-syllable loss.
- Optional third pass right-to-left windows if the global polish improves > 5 % (config flag).

### 4.4 Outputs

- Gesture score with `physio.tracks` at 1000 Hz: `resp.pressure`, `src.L.beta`, `src.L.alpha_direct` (fitting-only track, documented), `src.L.gate` = 1, `oec.freq_hz`, `beak.gape`, plus `settings` (bird constants) and `segments` (the annotation label).
- Fit log with all passes, per-term losses, evaluations, wall time.
- Summary table per syllable: total loss, per-term losses, f₀ median/p90 error (cents), entropy error, harmonic error, runtime, flags (buzz frames, two-voice candidate from D2).

### 4.5 Runner

- Job queue of syllables sorted by label; processes = `--jobs`; each job writes atomically (temp file + rename); `--resume` skips finished jobs; a crashed job is retried once, then marked failed with the traceback in the log.
- Progress printed and written to `build/logs/E3_<run>.log`.

## 5. Suggested steps

1. Implement on the benchmark; tune window size, knots and weights to beat E2 clearly.
2. Real data on 3 per label; inspect; adjust config (document every change and why).
3. Full dataset run in the background; meanwhile build the summary/listening tooling.
4. Summary tables, worst-case analysis, listening pages.

## 6. Out of scope

Bird constants optimisation and two-voice fits (E4); gestures (F-phase); gradient methods (G3).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | State carry correctness: evaluating a window from a loaded state gives the bit-identical audio to rendering the full syllable with the same coefficients (test). |
| 2 | Benchmark (clean): median total loss ≤ 50 % of E2 θ₀ (no polish); median f₀ error ≤ 10 cents; median control RMSE improved vs E2 (report per control). |
| 3 | Benchmark (20 dB + reverb): median total loss ≤ 70 % of E2 θ₀. |
| 4 | Real data: loss improved vs E2 θ₀ on ≥ 90 % of syllables; median f₀ error ≤ 15 cents on voiced frames (report per label). |
| 5 | Resume: interrupting the dataset run and restarting with `--resume` completes without redoing finished syllables. |
| 6 | All syllables of `zf_v1` processed (failures < 2 %, listed). Total runtime reported. |
| 7 | `scripts/check.sh` passes. `state.md` baseline "Real zebra finch resynthesis median MRSTFT loss" filled. |

## 8. Listening & results

- `listening/E3/`: for every label: the 3 best, 3 median and 3 worst syllables — target vs resynthesis (A/B), spectrograms with f₀ overlay, control curves figure.
- Report: per-label metrics table; benchmark table (E1 baseline, E2, E3); worst-case analysis (what goes wrong: onsets, noise, harmonic balance, two-voice, reverb…).

Final message: ask the human to listen to median and worst cases per label and rate each label's resynthesis 1–5; ratings go into the report's revision history and steer E4.

## 9. Pitfalls

- Using stale state after changing a coefficient that affects earlier samples: free coefficients must not touch samples before the saved state point (choose support rules accordingly, or re-render from an earlier checkpoint).
- Window losses near silence are dominated by noise: weight terms by target energy.
- Very long runs: watch disk usage of checkpoints; clean them after a job completes successfully.
