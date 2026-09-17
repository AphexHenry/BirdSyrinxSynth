# WP-B6 — Voice assembly, body size, state save/load, benchmarks

| Field | Value |
|---|---|
| Phase | B — The bird body |
| Depends on | B5 |
| Size | M |
| Milestone | **M2 — complete voice** |

## 1. Goal

Finish the Tier-1 voice as a product-grade component: the full `BirdVoice` API from the architecture (settings, control frames, telemetry), **coherent body-size scaling**, **state save/load** (needed by fitting), sample-rate and block-size robustness, golden renders, full Python streaming API, and CPU benchmarks proving the real-time budget.

## 2. Read first

- `01_research.md` **§4.1**, **§4.9**, §4.10, §6
- `02_architecture.md` **§6 (all)**, §7.1, §9.2, §10
- Reports of WP-B1 … WP-B5

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/model/body_scaling.hpp` | size scaling rules |
| `core/include/cuicui/model/bird_voice.hpp` | complete API: `ControlFrame`, `VoiceSettings`, `VoiceState`, `VoiceTelemetry`, `saveState/loadState`, `latencySamples` |
| `params.def` | add `body.size` |
| `apps/bird_bench/main.cpp` | benchmarks |
| `core/tests/golden/*.render.json` + `expected_features.json` | ≥ 6 golden renders covering all modules |
| `core/tests/test_voice_*.cpp` | tests §7 |
| `python/bindings/module.cpp` | `render_controls(...)`, `Voice` class (settings, process, save/load state, telemetry) |
| `python/tests/test_voice_api.py` | parity & state tests |
| `examples/phrases/*.render.json` | hand-made demo phrases (used for listening and later as regression material) |

## 4. Specification

### 4.1 Body size σ = `body.size`

| Quantity | Scaling |
|---|---|
| γ | γ₀ / σ (γ₀ = `src.gamma`) |
| trachea length | L · σ |
| UVT (glottis/beak lengths ∝ σ, areas ∝ σ²) | via `sizeScale` = σ |
| `oec.freq_hz` | effective $f_H$ = value / σ (the parameter is expressed for σ = 1) |
| tension-noise bandwidth, aspiration band-pass centre | ∝ 1/σ |
| respiratory time constants | ∝ √σ **[D]** |

Size is a slow parameter (smoothing 50 ms). All derived quantities must update smoothly (no clicks when sweeping σ).

### 4.2 Voice API (from `02_architecture.md` §6)

- `ControlFrame` holds every `fast` parameter; `VoiceSettings` the others. The registry marks which is which; keep them consistent (a test iterates the registry and checks every Physiology parameter is reachable through one of the two structs).
- `process(from, to, out, n)`: linear ramp from `from` to `to` over the n host samples, at internal rate.
- `VoiceState`: POD containing every state variable (oscillators, DC blockers, delay buffer + write index, UVT states, smoothers, noise filter states, RNG states, decimator histories). Serialisable to bytes (`std::memcpy`-safe; the delay buffer is a fixed-size array sized for the maximum length). Version field.
- `telemetry()`: α_L, α_R, β_L, β_R, p, f₀ estimate (zero-crossing on the dominant open side, updated every 10 ms), output level dB, non-finite reset count.

### 4.3 CLI determinism rule

`bird_render` evaluates automation **per host sample** (ramps of length 1), so results are independent of internal block size.

### 4.4 Benchmarks (`bird_bench`)

- Scenarios: single voice default note; voice with both sides + noise + fast β modulation; 16 voices.
- Host rates 44.1, 48, 96 kHz with OS auto; block size 256.
- Report: real-time factor, % of one core, worst block time vs block duration.

## 5. Suggested steps

1. Body scaling + tests.
2. Complete API structs, registry consistency test.
3. State save/load + tests; Python `Voice` + `render_controls`.
4. Golden renders & expected features; sample-rate/block-size tests.
5. Benchmarks; optimise if needed (e.g. avoid recomputing UVT matrices, `-O3`, remove branches).
6. Demo phrases + listening page.

## 6. Out of scope

Plugin (C1), atlas and pitch mapping (C2), gestures (C4+).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Size: σ = 2 vs σ = 1 at identical α, β: f₀ halves (±0.5 %); strongest trachea resonance and UVT peak halve (±2 %). σ sweep 0.5 → 2 over 2 s: energy above 18 kHz stays > 60 dB below the fundamental (no clicks). |
| 2 | State: render 2 s; save state at 1 s; load into a fresh voice and render 1–2 s → bit-identical to the original second half (C++ and Python). |
| 3 | Block size: CLI renders with internal block sizes 1, 17, 64, 512 are bit-identical. |
| 4 | Sample rate: golden renders at 44.1, 48, 96 kHz give f₀ within 2 cents and main spectral peaks within 1 % of each other. |
| 5 | Golden renders: ≥ 6 specs (single-side tonal, rich/buzzy with noise, two-voice, side switch, size 0.5 and 3, syllable train); features (median f₀, RMS dB, spectral centroid, 3 largest peaks) within tolerances stored in `expected_features.json`. |
| 6 | Registry consistency: every Physiology/Engine parameter is used by `ControlFrame` or `VoiceSettings`; every field maps to a registry ID. |
| 7 | CPU (Release, dev Mac, 48 kHz, OS 4): one voice ≤ 0.5 % of one core; 16 voices real-time factor ≤ 0.1; worst block time < 25 % of block duration. If not met: report numbers and profiling results, propose optimisation (human decides). |
| 8 | Fuzz 60 s of all parameters incl. size: no non-finite values. Zero allocations in `process()`. |
| 9 | Python `render_controls` output bit-identical to CLI for the golden specs. |
| 10 | `scripts/check.sh` passes. `state.md` "Baselines" updated with CPU numbers. |

## 8. Listening & results — milestone M2

Render into `listening/B6/` (48 kHz).

| File | Content | Listen for |
|---|---|---|
| `01_size_sweep_phrase.wav` | the same short phrase at σ = 0.5, 1, 2, 4 | a *bigger bird*, not a slowed-down tape: resonances and articulation scale together |
| `02_size_continuous.wav` | sustained note with σ swept 0.25 → 4 → 1 | smooth morph, no clicks |
| `03_demo_phrase_zf_like.wav` vs a real motif (D1) as reference | hand-automated phrase: 3 intro notes, a harmonic stack, a downsweep, a buzzy note, a high note | overall plausibility before any fitting or gestures |
| `04_demo_two_voice_phrase.wav` | phrase using side switching and a two-voice note | the two-sided syrinx in context |
| `05_sample_rates.wav` | golden render at 44.1/48/96 kHz, concatenated (resampled to 48 kHz for the page) | identical sound |

Figures: CPU table; size scaling spectra.

Final message: this is **milestone M2**. Ask the human for overall impressions of the complete voice and what matters most before playability work starts.

## 9. Pitfalls

- `VoiceState` must include decimator histories and smoother states, or reloads will click/differ.
- Size changes γ: the oscillator's RK4 accuracy depends on γ/f_int — check convergence at σ = 0.25 (γ = 96 000): if the pitch error exceeds 1 cent at OS 4, force a higher internal rate for small sizes and document (latency must stay constant for the host: choose the OS policy so latency does not change with σ, e.g. fixed OS per session).

## Notes from WP-B5 (2026-09-17)

- `VoiceState` still stores only the **left** labial `(x, y)`. The right oscillator, both DC blockers, tension RNGs, gates and the coupling smoother are not saved. B6 state save/load must extend this or two-voice reloads will click / differ.
- Side-switch demos at α = 0.2 cannot reach 1.5 kHz (β hits the silent floor). Use α ≈ 0.16, or per-side α offsets, when mapping a wide pitch jump on one air-sac pressure.
- Weak coupling lock range is tiny on this oscillator: at (α, β_L) = (0.2, 0.30), largest locking Δ was 0 / 0 / 0.01 for ε = 0.01 / 0.03 / 0.1 (0.005 grid, period-jitter test). Do not expect 8 Hz beating to lock unless Δβ is very small.
