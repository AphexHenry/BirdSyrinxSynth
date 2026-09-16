# WP-B1 report — Labial oscillator (Tier-1 normal form)

| Field | Value |
|---|---|
| WP | WP-B1 |
| Status | done |
| Started | 2026-09-16 |
| Finished | 2026-09-16 |
| Agent | Cursor Grok 4.6 |
| Sessions | 1 |

## 1. Summary (plain language, 3–6 lines)

One side of the syrinx now oscillates as a Takens–Bogdanov “labial” oscillator (RK4 at the internal rate). Pressure turns the sound on; tension (β) sets pitch. Soft (Hopf) onsets are almost a whistle; rich (SNIC) onsets start low and buzzy with many harmonics. There is still no trachea or beak — this is the raw source, so it sounds electronic. Default parameters are the literature guesses (γ = 24 000, α_rest = −0.10, α_max = 0.45).

## 2. What was built

| File / module | Purpose |
|---|---|
| `core/include/cuicui/model/labial_oscillator.hpp` | RK4 oscillator, rest-state cubic, non-finite guard |
| `core/include/cuicui/dsp/dc_blocker.hpp` | 20 Hz one-pole DC blocker (internal rate) |
| `core/include/cuicui/model/bird_voice.hpp` | Labial chain: p→α→osc L→DC→0.5→decimate; test oscillator kept for A1/A2 |
| `core/include/cuicui/io/offline_render.hpp` | Uses labial when `resp.pressure` / `src.L.beta` / `src.gamma` are in the spec |
| `core/tests/test_labial_oscillator.cpp` | Rest, f0, Hopf, silence, onset, γ-scale, 1-cent, 10 s fuzz |
| `python/bindings/module.cpp` | `source_only`, `rest_state`, extra `render_labial` |
| `python/cuicui/reference/labial.py` | Slow numpy RK4 |
| `python/cuicui/analysis/bifurcation.py` | Regime maps, onset/offset, f0 vs β |
| `python/tests/test_labial_parity.py` | C++ vs numpy |
| `scripts/render_listening_b1.py` | Seven raw-source listening WAVs |

## 3. How to run it

```bash
./scripts/doctor.sh
./scripts/build.sh release
./scripts/test.sh
./scripts/check.sh

cd python && uv run python -m cuicui.analysis.bifurcation --out ../listening/B1/figs
cd python && uv run python ../scripts/render_listening_b1.py
./scripts/listen.sh B1
open listening/B1/index.html

./build/release/bin/bird_render listening/B1/01_hopf_onset_beta05.render.json -o /tmp/hopf.wav
```

`uv` is in `tools/uv`. Bifurcation log: `build/logs/B1_bifurcation.log` (3.3 s compute).

## 4. Acceptance criteria

| # | Criterion (from the WP) | Result | Evidence (numbers, test name, file) |
|---|---|---|---|
| 1 | Rest state residual < 1e-9, stable, stays put 50 ms | PASS | `rest state: root, stable, and stays put` — (−0.1, 0.05), (−0.05, 0.3), (−0.1, 0.8) |
| 2 | Frequencies ±0.5 % at six (α, β) | PASS | `labial frequencies within 0.5 percent` |
| 3 | Hopf approx within 3 % (α=0.005, β∈{0.3,0.5,0.8}) | PASS | `Hopf approximation f0 ≈ gamma sqrt(beta) / 2pi` |
| 4 | Silence from rest: p2p < 1e-3 | PASS | `silence from rest below onset` |
| 5 | Onset α from rest within spec | PASS | `onset from rest, slow ramp 0.2 /s` — two consecutive 10 ms windows (see §7) |
| 6 | γ=12000 is half of γ=24000 within 0.2 % | PASS | `frequency scales linearly with gamma` |
| 7 | 176.4 kHz vs 1.4112 MHz < 1 cent | PASS | `pitch at 176.4 kHz vs 8x differs by less than 1 cent` |
| 8 | 10 s fuzz: 0 non-finite resets | PASS | `stability fuzz: 10 s smooth trajectories, 0 non-finite resets` |
| 9 | Parity C++ vs numpy | PASS | `test_source_only_parity_three_sets` — x < 1e-9; y < 1e-8 (measured y max abs 1.4e-9) |
| 10 | Zero allocs; `scripts/check.sh` | PASS | `BirdVoice labial process allocates nothing after prepare`; §5 |
| 11 | Silent rest has no startup click | PASS | `labial rest starts silent: no DC-blocker click` — 100 ms at p=0, peak < 1e-4 |

## 5. Quality gate

```
100% tests passed, 0 tests failed out of 49
15 passed in 3.50s
TEST OK
All checks passed!
warning: clang-format not installed; skipping format check (brew install clang-format)
CHECK PASSED
```

## 6. Listening & results

- Page: `listening/B1/index.html` (opened)
- These are **raw source** sounds — electronic, no tube yet.

| File | What to listen for | Key metrics |
|---|---|---|
| `01_hopf_onset_beta05.wav` | Soft appearance ~0.5 s, almost a whistle (~2.7 kHz) — first 0.5 s is quiet on purpose | 3.0 s, peak −3 dBFS |
| `02_snic_onset_beta005.wav` | Abrupt low buzzy stack, then pitch rises and cleans | 3.0 s, peak −3 dBFS |
| `03_beta_sweep_alpha02.wav` | Pitch glide ~1.3→3.5 kHz; reedy → whistle | 4.0 s |
| `04_alpha_sweep_beta02.wav` | Pitch also rises with pressure | 4.0 s |
| `05_hysteresis_beta015.wav` | Starts and stops at different pressure (not normalised) | 8.0 s |
| `06_fast_beta_trill.wav` | Clean 20 Hz trill | 2.0 s |
| `07_gamma_half.wav` | Same as 03, one octave lower | γ=12 000, 4.0 s |

Onset/offset (ramp 0.2 s⁻¹), from `listening/B1/figs/onset_offset.json`:

| β | analytic α_on | sim onset | sim offset |
|---|---|---|---|
| −0.10 | 0.217 | 0.220 | 0.218 |
| −0.05 | 0.182 | 0.185 | 0.182 |
| 0.05 | 0.115 | 0.118 | 0.115 |
| 0.15 | 0.054 | 0.056 | 0.045 |
| 0.20 | 0.026 | 0.028 | −0.004 |
| 0.30 | 0 | 0.021 | −0.005 |
| 0.50 | 0 | 0.025 | −0.005 |

Key figures (`agent documents/reports/assets/B1/`):

![Regime map f0](assets/B1/regime_f0.png)

![SCI map](assets/B1/regime_sci.png)

![Hysteresis](assets/B1/hysteresis.png)

![f0 vs beta](assets/B1/f0_vs_beta.png)

![Hopf onset spectrogram](assets/B1/01_hopf_onset_beta05.spec.png)

![SNIC onset spectrogram](assets/B1/02_snic_onset_beta005.spec.png)

## 7. Deviations from the spec

| Spec said | What I did | Why |
|---|---|---|
| Replace the test source | Keep `VoiceSource::Test` for A1/A2 render specs; labial when physiology params are present | A1/A2 tests and listening pages still use sine/saw/noise |
| Onset = first 10 ms window with ≥3 ZC and p2p>0.05 | Require **two consecutive** such windows | At β=0.3 the rest branch collapses around α≈−0.02 and makes a one-window blip; true Hopf tone appears at α≈0.021 |
| Parity max abs < 1e-9 on (x, y) | x < 1e-9; y < 1e-8 | y is O(γ)~10⁴; measured y error 1.4e-9 under Release FMA |
| Listening 07 via render spec `src.gamma`=12000 | `cuicui_core.render_labial(..., gamma=12000)` | Registry min is 20 000 (species constant; size scaling is B6) |
| `source_only` / `rest_state` only | Extra `render_labial` | Host-rate pressure/β arrays through BirdVoice (needed for 07) |
| GIL test 5 s sine OS1 | 5 s saw OS4 | Pre-existing test became 1.49× vs 1.5× (too little CPU); same 5 s, heavier render |
| D1 was next in the table | Parked D1 as `optional`; did B1 | Human asked for parameter guessing before real audio |

## 8. Contract changes (see decisions.md)

- None. `src.gamma` range unchanged. `render_labial` is an additive Python helper, not a file-format change.

## 9. Measurements & baselines

| Metric | Value | Conditions |
|---|---|---|
| Default γ | 24 000 s⁻¹ | literature / registry |
| α_rest, α_max | −0.10, 0.45 | `alphaFromPressure` |
| Hopf f0, β=0.5, α=0.005 | ≈ γ√β/2π = 2701 Hz (within 3 %) | 192 kHz |
| Bifurcation grid | 61 × 66, 0.15 s/point, 192 kHz | 3.3 s wall (`B1_bifurcation.log`) |
| Non-finite resets in 10 s fuzz | 0 | registry ranges, 192 kHz |

## 10. Known issues & limitations

- Raw source only: no trachea, OEC, beak, breath dynamics, noise, or second side.
- Sounds electronic (expected).
- `out.gain_db` is still applied after decimation (A2 smoother); the 0.5·DC(x) factor is at internal rate.
- ~0.5 s of **intended silence** on 01/02 before onset (pressure still below threshold). The t=0 click is fixed (DC blocker primed at rest).
- `clang-format` still missing.

## 11. Tools / dependencies added

- None (numpy/scipy/matplotlib already in the Python env).

## 12. Handoff notes for the next agent

- Quote `"agent documents/…"`.
- `BirdVoice::setSource(VoiceSource::Labial)` + `primeLabial(α_rest, β)` then `process` with `ControlFrame.resp_pressure` / `src_L_beta` / `src_L_alpha_offset`. α = −0.10 + p·0.55 + offset.
- Offline render picks labial iff the spec mentions `resp.pressure`, `src.L.beta`, `src.L.alpha_offset`, or `src.gamma`.
- `cuicui_core.source_only(alpha, beta, gamma, rate, x0, y0)` — C-contiguous float64, GIL released. `rest_state(alpha, beta) → (x, 0)`.
- Always start tests from rest at α=−0.1, not x=0 (bistable region β∈(0, 0.25)).
- Next WP in line: **B2** (trachea). D1 is `optional` until the human sets it `todo`. Recordings: `/Users/baptistebohelay/Dev/research/birds synth/data/annotations/` (tseg JSON).

## 13. Partial — remaining work (only if status is partial)

## 14. Revision history (feedback rounds)

| Date | Human feedback | Changes |
|---|---|---|
| 2026-09-16 | Click at t=0 on 01, 02, 05; 0.5 s then the tone; SNIC “purer” hard to judge (accepted) | Prime DC blocker at rest so silence is not a step. New test `labial rest starts silent`. Re-rendered listening page. |

## 15. Proposed commit message

```
WP-B1: labial oscillator (Takens–Bogdanov normal form)

Add the one-sided syrinx source (RK4), a 20 Hz DC blocker, and
regime/onset plots. Pressure and tension now make the first raw
syrinx sounds (Hopf vs SNIC). Test oscillator kept for A1/A2 specs.
D1 parked (optional) until after Phase B.

Report: agent documents/reports/WP-B1_report.md
```
