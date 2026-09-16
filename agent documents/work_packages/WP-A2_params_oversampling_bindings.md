# WP-A2 — Parameter registry, control signals, oversampling, Python bindings

| Field | Value |
|---|---|
| Phase | A — Foundations |
| Depends on | A1 |
| Size | M |
| Milestone | – |

## 1. Goal

Put in place the technical backbone every DSP module will use: the **single parameter registry**, parameter smoothing and control interpolation, the **oversampling/decimation** framework (internal rate ≥ 176.4 kHz), a skeleton `BirdVoice` that renders at the internal rate, and the **Python bindings** so that Python runs the exact same C++ engine.

## 2. Read first

- `01_research.md` §6 (numerics, real time)
- `02_architecture.md` §5, §6 (incl. §6.1), §7 (registry), §8, §10
- Report of WP-A1 (handoff notes)

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/params.def`, `params.hpp` | X-macro registry; `ParamId` enum; `ParamSpec {id, name, unit, min, max, def, skew, smoothingMs, layer, fast, sinceWP}`; `findParam("src.L.beta")`; `clamp`, `toNormalised`/`fromNormalised` (with skew) |
| `core/include/cuicui/dsp/smoother.hpp` | one-pole parameter smoother (time constant in ms, `setTarget`, `next()`, `snap()`) |
| `core/include/cuicui/dsp/control_ramp.hpp` | linear interpolation of a `ControlFrame`-like struct across a block at internal rate |
| `core/include/cuicui/dsp/halfband.hpp` | linear-phase FIR half-band decimator (Kaiser design at `prepare`, polyphase implementation) |
| `core/include/cuicui/dsp/oversampling.hpp` | `resolveOversampling(hostRate, requested)`, decimator cascade for OS = 2, 4, 8, latency query |
| `core/include/cuicui/model/bird_voice.hpp` | skeleton `BirdVoice` (API of `02_architecture.md` §6) whose inner source is, for now, a test oscillator running at the internal rate |
| `core/include/cuicui/model/test_saw.hpp` | naive (non-band-limited) sawtooth at internal rate — for aliasing tests only |
| `apps/bird_render` | now uses the registry: any registered parameter can be set/automated; `--dump-params` prints JSON; `--oversampling auto|1|2|4|8` |
| `core/tests/alloc_counter.hpp` + test | global `operator new/delete` override counting allocations during `process()` |
| `python/pyproject.toml` | build backend `scikit-build-core` + `nanobind` building module `cuicui_core` from `python/bindings/` |
| `python/bindings/module.cpp` | `param_specs()`, `render_spec(dict)`, `resolve_oversampling(rate, requested)` (more functions added by later WPs) |
| `python/tests/test_bindings.py` | parity and GIL tests |

Parameters added to `params.def` in this WP: `engine.oversampling`, `engine.seed`, `out.gain_db` (range from `02_architecture.md` §7.1), plus test-only parameters `test.freq_hz` (20–20000, default 440), `test.gain_db` (−96–0, default −12), `test.waveform` (enum sine/saw, default sine) with layer `Engine`. Test parameters are clearly marked and may be removed in G1.

## 4. Specification

### 4.1 Registry

- Exactly one line per parameter in `params.def`; macro columns as in `02_architecture.md` §7.
- Skew mapping for log parameters: store `skew` so that `fromNormalised(u) = min + (max − min) · u^(1/skew)` (JUCE-compatible convention), or an explicit `log` flag — pick one, document it in the header, test round-trip.
- `--dump-params` JSON keys: `id, name, unit, min, max, default, skew, smoothing_ms, layer, fast, since`.

### 4.2 Smoothing and control interpolation

- One-pole smoother: $y[n] = y[n-1] + (1 - e^{-1/(\tau f)})\,(x - y[n-1])$, τ in seconds, f the rate at which `next()` is called.
- `ControlRamp`: given `from` and `to` values and `numInternalSamples`, returns linearly interpolated values per internal sample (no allocation).

### 4.3 Oversampling policy and decimation

- `resolveOversampling(hostRate, requested)`: if requested ∈ {1,2,4,8} use it; if auto (0) → smallest power of two with `hostRate·OS ≥ 176400` (cap 8).
- Decimation by 2 per stage (cascade). Each stage: linear-phase FIR half-band, Kaiser window, designed at `prepare()`:
  - for a stage decimating from $F_{in}$ to $F_{out} = F_{in}/2$, content in $[F_{out}/2, F_{in}/2]$ folds to $F_{out} - f$. So that no alias lands in 0–20 kHz: pass band edge $f_p$ = 20 kHz and stop band edge $f_{stop} = F_{out} - 20$ kHz (half-band symmetry $f_p + f_{stop} = F_{out}$ holds automatically). Early stages (e.g. 192 → 96 kHz, stop band from 76 kHz) have a wide transition band and need few taps; the last stage (e.g. 96 → 48 kHz, stop band from 28 kHz) needs the most taps. Document the tap counts per stage.
  - stop band attenuation ≥ 90 dB (Kaiser β from the standard formula $\beta = 0.1102(A - 8.7)$ for A > 50).
  - pass band ripple ≤ 0.1 dB (0–20 kHz).
  - polyphase/zero-skipping implementation (half-band: every other tap except centre is zero).
- Latency: sum of group delays converted to host samples (rounded); exposed via `latencySamples()`.

### 4.4 Skeleton BirdVoice

- `prepare(spec)`: resolves OS, allocates buffers for `maxBlockSize·OS`, prepares decimators.
- `process(from, to, out, n)`: for each host sample, OS internal samples of the source (test sine or naive saw at internal rate) with linearly ramped controls → decimate → `out`.
- Later WPs replace the test source by the anatomical chain; the structure stays.

### 4.5 Python bindings

- nanobind module `cuicui_core` built by `uv sync` / `uv pip install -e python` through scikit-build-core, reusing the top-level CMake or a dedicated `python/CMakeLists.txt` that includes `core/`.
- `render_spec(spec: dict) -> numpy.ndarray[float32]` releases the GIL during rendering (`nb::gil_scoped_release`).
- Output must be **bit-identical** to `bird_render` for the same spec.

## 5. Suggested steps

1. Registry + tests + `--dump-params`.
2. Smoother, control ramp + tests.
3. Half-band design + decimator cascade + frequency-response tests.
4. Skeleton `BirdVoice` + test sources + allocation-counting test.
5. Python bindings + parity test.
6. Listening renders.

## 6. Out of scope

Any bird model (B1+), JUCE (C1), upsampling of external audio (not needed).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | `bird_render --dump-params` JSON equals `cuicui_core.param_specs()` (test). Normalise/denormalise round trip error < 1e-6 for every parameter. |
| 2 | Smoother: step response reaches 63.2 % after τ within ±2 %. |
| 3 | Auto oversampling: 44 100→4, 48 000→4, 88 200→2, 96 000→2, 176 400→1, 192 000→1. |
| 4 | Decimator (OS 2, 4, 8; host 44.1 and 48 kHz): pass band ripple ≤ 0.1 dB from 20 Hz to 20 kHz (sine sweep measurement); for sines placed at internal-rate frequencies whose alias would land in 0–20 kHz, alias level ≤ −90 dB relative to a 1 kHz reference. |
| 5 | Reported latency equals the measured impulse-response peak position (±1 host sample). |
| 6 | Naive saw 3 kHz: with OS = 1 at 48 kHz vs OS = 4 at 48 kHz, energy of non-harmonic (alias) components below 20 kHz is reduced by ≥ 40 dB. |
| 7 | Zero heap allocations during `BirdVoice::process()` after `prepare()` (allocation-counting test). |
| 8 | Python `render_spec` output is bit-identical to `bird_render` output for 3 specs (sine, saw OS 1, saw OS 4). |
| 9 | GIL released: rendering two 5 s specs in two Python threads is ≥ 1.5× faster than sequentially (or document why the machine cannot show it and show the `gil_scoped_release` usage). |
| 10 | `scripts/check.sh` passes. |

## 8. Listening & results

Render into `listening/A2/` (48 kHz):

| File | Content | Listen for |
|---|---|---|
| `01_saw_sweep_os1.wav` | naive saw sweeping 200 Hz → 8 kHz over 4 s, OS = 1 | audible inharmonic "whistling" aliases going *down* while the pitch goes up |
| `02_saw_sweep_os4.wav` | same with OS = 4 | aliases strongly reduced; brighter but clean |
| `03_gain_steps_unsmoothed.wav` | 1 kHz sine, `out.gain_db` jumps every 250 ms, smoothing 0 ms | zipper clicks at each jump |
| `04_gain_steps_smoothed.wav` | same with registry smoothing | no clicks |

Put 01/02 in the same `group` and add a plot of the decimator's magnitude response (0–96 kHz, dB) to the page and report.

## 9. Pitfalls

- Half-band design edge cases: make sure the last stage has the narrow transition band (20 kHz → f_out − 20 kHz); earlier stages can be cheap.
- Keep `bird_render` and the Python module linked to the *same* header code (no duplicated logic in the binding).
- scikit-build-core + uv on macOS: set `CMAKE_OSX_ARCHITECTURES` only if needed; build type Release for the Python module.

## Notes from WP-A1

- `bird_render` lives at `build/<preset>/bin/bird_render`. Scripts prepend `tools/uv` to PATH (`scripts/_common.sh`); uv itself is not on the default macOS PATH.
- `test.waveform` already exists as an A1 parameter (`sine` | `noise`), parsed in `render_spec.hpp` like `test.freq_hz` / `test.gain_db`. A2 should move it into the registry and add `saw`. A top-level render-spec `generator` field was rejected (D-013).
- WAV writer: float32 (format 3) and PCM24, little-endian, `fmt ` size 16. Python tests call the CLI via `build/release/bin/bird_render`.
