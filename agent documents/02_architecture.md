# Part 0 — Architecture, conventions and shared contracts

> Status: **contract document**. Everything here is shared by all work packages (WPs).
> An agent may only change a contract (parameter IDs, APIs listed here, file formats, repo layout, tooling) by:
> 1. writing a decision entry in `decisions.md` (status `proposed`),
> 2. explaining it in the WP report under "Contract changes",
> 3. getting human approval together with the commit.
> Additive changes (new parameter, new optional JSON field) follow the same rule but are expected and normal.

---

## 1. Technology stack

| Area | Choice | Why |
|---|---|---|
| DSP core | **C++20**, header-only library `cuicui_core`, **no JUCE dependency** | reusable in plugin, CLI, Python bindings and tests; the exact same code runs everywhere |
| Plugin framework | **JUCE 8.x** (pin an exact tag in C1, via CMake FetchContent; not needed before C1) | industry standard, well known by coding agents, VST3 + AU + Standalone |
| Formats | **VST3, AU, Standalone**; **CLAP** via `clap-juce-extensions` (or native JUCE CLAP if the pinned JUCE version supports it — check in C1) | AU needed for Logic Pro; CLAP for Bitwig/Reaper |
| Build | **CMake ≥ 3.25**, **Ninja**, presets in `CMakePresets.json` | reproducible command-line builds for agents |
| C++ tests | **Catch2 v3** (FetchContent) | simple, fast |
| JSON (C++) | **nlohmann/json** (FetchContent), offline/IO code only, never on the audio thread | |
| Python | **Python ≥ 3.11**, managed with **uv** (`python/pyproject.toml`) | fast, reproducible environments |
| Bindings | **nanobind** + **scikit-build-core** | same C++ engine in Python for analysis, atlas, fitting |
| Python libs | numpy, scipy, soundfile, librosa, matplotlib, pandas, pyarrow, jsonschema, cma (pycma), pytest, ruff | |
| Plugin validation | **pluginval** (downloaded into `tools/`), `auval` on macOS | |
| VCS | git, single branch `main`, one commit per approved WP | |
| Platform | **macOS (Apple Silicon) primary**; core + Python must also build on Linux; Windows later | the dev machine is a MacBook |

Agent environment: **Cursor** with a coding agent (Grok 4.6) running shell commands on the Mac.

---

## 2. Environment setup (macOS)

Checked by `scripts/doctor.sh` (created in WP-A1). Required:

- Xcode (full app, for AU validation) or at least Command Line Tools: `xcode-select -p`
- Homebrew packages: `cmake`, `ninja`, `uv`, `git` — install with `brew install cmake ninja uv` if missing
- pluginval: downloaded by `scripts/get_pluginval.sh` into `tools/pluginval/` (not committed)

Rules for agents: never use `sudo`; installing user-level developer tools via Homebrew or uv is allowed but **must be listed in the report**. If something needs the human (license acceptance, system dialogs, Xcode install), stop and ask.

---

## 3. Repository layout

```
<project root>/
├─ agent documents/          # specs, protocol, state, decisions, backlog, reports
├─ CMakeLists.txt
├─ CMakePresets.json
├─ cmake/                    # FetchContent deps, warnings, asset embedding
├─ core/                     # header-only C++20 DSP library "cuicui_core" (NO JUCE)
│  ├─ include/cuicui/
│  │  ├─ params.def          # parameter registry (X-macro) — single source of truth
│  │  ├─ params.hpp          # ParamId enum, ParamSpec table, helpers
│  │  ├─ dsp/                # one_pole.hpp, dc_blocker.hpp, lagrange.hpp, halfband.hpp, rng.hpp, smoother.hpp, biquad.hpp
│  │  ├─ model/              # respiratory_drive.hpp, labial_oscillator.hpp, bilateral_source.hpp, noise_sources.hpp,
│  │  │                      # trachea.hpp, upper_vocal_tract.hpp, bird_voice.hpp, body_scaling.hpp
│  │  ├─ control/            # atlas.hpp, inverse_map.hpp, voice_controller.hpp, gesture.hpp, archetypes.hpp,
│  │  │                      # gesture_engine.hpp, voice_bank.hpp, grammar.hpp
│  │  ├─ io/                 # render_spec.hpp, gesture_score.hpp, voice_bank_io.hpp (offline only)
│  │  └─ generated/          # generated headers (atlas tables) — produced by scripts, committed
│  └─ tests/                 # Catch2 tests (test_*.cpp)
├─ apps/
│  ├─ bird_render/           # CLI: render spec / gesture score → WAV (core only)
│  ├─ bird_bench/            # CPU benchmarks
│  └─ plugin_render/         # offline render through the plugin processor with MIDI + params (from C1)
├─ plugin/                   # JUCE plugin (processor, editor, presets, look-and-feel)
├─ python/
│  ├─ pyproject.toml
│  ├─ bindings/              # nanobind sources → Python module `cuicui_core`
│  ├─ cuicui/                # Python package
│  │  ├─ listen/             # listening kit: spectrograms, manifest, HTML page
│  │  ├─ reference/          # slow numpy reference implementations for parity tests
│  │  ├─ data/               # dataset ingestion (D1)
│  │  ├─ features/           # feature extraction (D2)
│  │  ├─ atlas/              # atlas generation (C2, E2)
│  │  ├─ fit/                # fitting pipeline (E1–E4)
│  │  └─ gestures/           # gesture extraction, FPCA, grammar, voice bank export (F1–F2)
│  └─ tests/
├─ assets/                   # small committed assets: atlas, factory voice banks, presets
├─ schemas/                  # JSON schemas for all file formats below
├─ data/
│  ├─ raw/                   # original recordings + annotations (NOT committed)
│  ├─ interim/               # clips, caches (NOT committed)
│  ├─ features/              # feature files (NOT committed unless small)
│  ├─ fits/                  # fit results (NOT committed unless small; summaries committed in reports)
│  └─ manifest/              # dataset manifests (committed)
├─ scripts/                  # doctor.sh, build.sh, test.sh, check.sh, validate_plugin.sh, get_pluginval.sh
├─ tools/                    # downloaded tools (NOT committed)
├─ listening/                # listening outputs per WP (NOT committed)
└─ build/                    # (NOT committed)
```

Names: C++ namespace `cuicui` (sub-namespaces `cuicui::dsp`, `cuicui::model`, `cuicui::control`, `cuicui::io`); Python package `cuicui`; binding module `cuicui_core`.

---

## 4. Scripts (the agent's standard commands)

All scripts are bash, run from the project root, exit non-zero on failure, and print a one-line summary at the end.

| Script | Does |
|---|---|
| `scripts/doctor.sh` | checks tools and versions, prints what is missing |
| `scripts/build.sh [debug\|release]` | configures (CMake preset) and builds core tests, apps, plugin (plugin target added in C1), Python module |
| `scripts/test.sh` | runs `ctest` and `uv run pytest` |
| `scripts/check.sh` | build (release) + tests + `clang-format --dry-run` (if installed) + `uv run ruff check` → **must pass before asking for approval** |
| `scripts/validate_plugin.sh` | pluginval (strictness level given by argument, default 5) on VST3 and AU, plus `auval` for AU |
| `scripts/listen.sh <WP-ID>` | builds the listening page from `listening/<WP-ID>/manifest.json` |

---

## 5. Coding conventions

### 5.1 C++

- C++20, `-Wall -Wextra -Wpedantic` (warnings as errors in CI-like `check.sh` for `core/` and `apps/`).
- Header-only core; each module one header; `#pragma once`.
- Audio-thread code: `noexcept`, no allocation, no locks, no exceptions, no `std::function` construction, no logging.
- State variables of oscillators/filters: `double`. Audio buffers: `float`.
- Every module has `prepare(double internalRate)` (or `prepare(const ProcessSpec&)`), `reset()`, and a per-sample or per-block processing function.
- Parameters are passed explicitly (plain structs), modules do not read global state.
- Comments reference the research document sections, e.g. `// see 01_research.md §4.6`.
- Format: `.clang-format` (LLVM based, 4 spaces, 110 columns) created in A1.

### 5.2 Python

- Type hints everywhere, `ruff` clean, docstrings for public functions.
- Pure functions for analysis; no hidden global state; random seeds explicit.
- Heavy loops use numpy vectorisation or the C++ engine through `cuicui_core`.
- Long-running jobs (atlas, fitting) are resumable: write intermediate results to disk, skip work already done.

---

## 6. Core C++ API contracts

These signatures are the contract; implementations may add private helpers.

```cpp
namespace cuicui {

struct ProcessSpec {
    double hostSampleRate = 48000.0;
    int    maxBlockSize   = 512;
    int    oversampling   = 0;      // 0 = auto (01_research §6.1), else 1,2,4,8
    double internalRate() const;    // hostSampleRate * resolved oversampling
    int    resolvedOversampling() const;
};

// Full set of physiological controls for one voice at one instant (layer 1).
// Field names mirror parameter IDs (dots → underscores).
struct ControlFrame {
    float resp_pressure = 0.0f;
    float src_L_beta = 0.3f, src_R_beta = 0.3f;
    float src_L_gate = 1.0f, src_R_gate = 0.0f;
    float src_L_alpha_offset = 0.0f, src_R_alpha_offset = 0.0f;
    float oec_freq_hz = 3000.0f;
    float beak_gape = 0.5f;
    float noise_beta_sigma = 0.005f;
    // ... every layer-1 parameter that can change quickly (see §7, column "fast")
};

// Slow / constant per-voice settings (anatomy, engine options).
struct VoiceSettings {
    double body_size = 1.0, src_gamma = 24000.0;
    double trachea_length_mm = 33.0, trachea_reflection = 0.5, trachea_loss = 0.3, trachea_input_gain = 1.0;
    double oec_damping = 0.1, beak_damping = 0.3;
    double out_radiation = 1.0, out_gain_db = 0.0;
    double resp_attack_ms = 8.0, resp_release_ms = 15.0;
    double noise_beta_bandwidth_hz = 300.0, noise_aspiration = 0.02, src_coupling = 0.0;
    uint64_t seed = 1;
};

struct VoiceTelemetry {       // read by UI / tests (lock-free copy)
    float alpha_L, alpha_R, beta_L, beta_R, pressure, f0_estimate_hz, level_db;
    uint32_t nonFiniteResets;
};

struct VoiceState;            // POD snapshot of every internal state variable (oscillators, delay lines, filters, RNG)

class BirdVoice {
public:
    void prepare(const ProcessSpec& spec);                 // allocates; not real-time
    void reset(uint64_t seed);                             // rest state, deterministic
    void setSettings(const VoiceSettings& s) noexcept;     // smoothed internally where needed
    // Renders numSamples host-rate samples; controls are linearly interpolated from `from` to `to`.
    void process(const ControlFrame& from, const ControlFrame& to, float* out, int numSamples) noexcept;
    void saveState(VoiceState& s) const noexcept;          // for fitting warm starts (E3)
    void loadState(const VoiceState& s) noexcept;
    VoiceTelemetry telemetry() const noexcept;
    int latencySamples() const noexcept;                   // decimation filter latency at host rate
};

} // namespace cuicui
```

Individual modules (`LabialOscillator`, `Trachea`, …) expose the minimal API needed by `BirdVoice` and by their unit tests; their exact signatures are defined in their WPs.

### 6.1 Python binding contract (`cuicui_core`)

```python
import cuicui_core as cc

cc.param_specs() -> list[dict]                     # id, name, unit, min, max, default, smoothing_ms, layer, fast
cc.render_spec(spec: dict) -> np.ndarray            # render a render-spec JSON (§9.2), float32 mono at spec sample_rate
cc.render_controls(settings: dict, controls: dict[str, np.ndarray], control_rate_hz: float,
                   sample_rate: float, seed: int, oversampling: int = 0,
                   state: bytes | None = None) -> tuple[np.ndarray, bytes]   # audio, final state
cc.Voice(sample_rate, oversampling=0)               # streaming object: .set_settings(dict), .process(frame_from, frame_to, n),
                                                    # .save_state() -> bytes, .load_state(bytes), .telemetry()
cc.source_only(alpha: np.ndarray, beta: np.ndarray, gamma: float, internal_rate: float,
               x0: float, y0: float) -> tuple[np.ndarray, np.ndarray]    # oscillator only, for atlas (C2)
```

The binding must release the GIL during rendering (so Python multiprocessing/threads can parallelise).

---

## 7. Parameter registry (single source of truth)

Defined once in `core/include/cuicui/params.def` as an X-macro:

```cpp
// CUICUI_PARAM(enumName, "string.id", "Display name", unit, min, max, default, skew, smoothing_ms, layer, fast, sinceWP)
CUICUI_PARAM(SrcLBeta, "src.L.beta", "Tension L", "", -0.3, 1.2, 0.3, 1.0, 1.0, Layer::Physiology, true, "B1")
```

From it are generated: the C++ `ParamId` enum and spec table, the Python `param_specs()`, the JUCE `AudioProcessorValueTreeState` layout (C1), and `bird_render --dump-params` (JSON). **Adding a parameter = one line in `params.def`** + the code that uses it + a row below.

`layer`: `Physiology` (1), `Performance` (3), `Engine` (expert/technical). `fast`: part of `ControlFrame` (changes at audio rate) vs `VoiceSettings`. "Driven": in Perform mode the pitch-led controller writes this parameter; the host parameter then acts as an offset or is shown read-only (defined in C3).

### 7.1 Layer 1 — physiology

| ID | Unit | Range | Default | Smooth (ms) | Fast | Driven in Perform | Since |
|---|---|---|---|---|---|---|---|
| `resp.pressure` | – | 0 – 1 | 0 | 2 | yes | yes | B1 (linear), B4 (dynamics) |
| `resp.attack_ms` | ms | 1 – 100 | 8 | – | no | no | B4 |
| `resp.release_ms` | ms | 1 – 200 | 15 | – | no | no | B4 |
| `src.gamma` | s⁻¹ | 20000 – 26000 | 24000 | 20 | no | no (scaled by size) | B1 |
| `src.L.beta` | – | −0.3 – 1.2 | 0.3 | 1 | yes | yes | B1 |
| `src.L.alpha_offset` | – | −0.3 – 0.3 | 0 | 2 | yes | yes | B1 |
| `src.R.beta` | – | −0.3 – 1.2 | 0.3 | 1 | yes | yes | B5 |
| `src.R.alpha_offset` | – | −0.3 – 0.3 | 0 | 2 | yes | yes | B5 |
| `src.L.gate` | – | 0 – 1 | 1 | 1 | yes | yes | B5 |
| `src.R.gate` | – | 0 – 1 | 0 | 1 | yes | yes | B5 |
| `src.coupling` | – | 0 – 0.2 | 0 | 20 | no | no | B5 |
| `noise.beta_sigma` | – | 0 – 0.05 | 0.005 | 20 | yes | partly (Tone/buzz) | B4 |
| `noise.beta_bandwidth_hz` | Hz | 50 – 2000 | 300 | – | no | no | B4 |
| `noise.aspiration` | – | 0 – 1 | 0.02 | 20 | no | no | B4 |
| `trachea.length_mm` | mm | 10 – 120 | 33 | 20 | no | no (scaled by size) | B2 |
| `trachea.reflection` | – | −0.95 – 0.95 | 0.5 | 20 | no | no | B2 |
| `trachea.loss` | – | 0 – 1 | 0.3 | 20 | no | no | B2 |
| `trachea.input_gain` | – | 0 – 4 | 1 | 20 | no | no | B2 |
| `oec.freq_hz` | Hz | 300 – 10000 (log) | 3000 | 2 | yes | yes (tracking) | B3 |
| `oec.damping` | – | 0.01 – 1 | 0.1 | 20 | no | no | B3 |
| `beak.gape` | – | 0 – 1 | 0.5 | 2 | yes | yes (tracking) | B3 |
| `beak.damping` | – | 0.01 – 1 | 0.3 | 20 | no | no | B3 |
| `out.radiation` | – | 0 – 1 | 1 | 20 | no | no | B3 |
| `out.gain_db` | dB | −48 – 12 | 0 | 20 | no | no | A2 |
| `body.size` | – | 0.25 – 4 (log) | 1 | 50 | no | no | B6 |
| `engine.oversampling` | enum | auto, 1, 2, 4, 8 | auto | – | no | no | A2 |
| `engine.seed` | int | 0 – 2³¹−1 | 1 | – | no | no | A2 (used from B4) |

Also registered: test-only parameters `test.freq_hz`, `test.gain_db`, `test.waveform` (A2, layer Engine, removed in G1), and `engine.source_model` if WP-G2 is scheduled. **Fitting-only track IDs** `src.L.alpha_direct` / `src.R.alpha_direct` (E1) are not host parameters: they are accepted only by `render_controls` and in gesture-score `physio.tracks`.

### 7.2 Layer 3 — performance

| ID | Unit | Range | Default | Since |
|---|---|---|---|---|
| `perf.mode` | enum | perform, physiology | perform | C3 |
| `perf.breath` | – | 0 – 1 | 0.7 | C3 |
| `perf.tone` | – | 0 – 1 | 0.6 | C3 |
| `perf.physics` | – | 0 – 1 | 0.5 | C3 |
| `perf.motion` | – | 0 – 1 | 0.3 | C3 |
| `perf.range` | – | 0 – 2 | 1 | C3 |
| `perf.speed` | – | 0.25 – 4 (log) | 1 | C3 |
| `perf.snap` | – | 0 – 1 | 0 | C3 |
| `perf.glide_ms` | ms | 0 – 500 | 30 | C3 |
| `perf.bend_range_st` | semitones | 0 – 24 | 2 | C3 |
| `perf.scale` | enum | chromatic, major, minor, pentatonic_major, pentatonic_minor, whole_tone | chromatic | C3 |
| `perf.root` | pitch class | 0 – 11 | 0 | C3 |
| `perf.oec_tracking` | – | 0 – 1 | 0.7 | C3 |
| `perf.gape_tracking` | per octave | 0 – 0.5 | 0.15 | C3 |
| `perf.side_switch_cents` | cents | 100 – 2400 | 700 | C3 |
| `perf.range_fold` | bool | off/on | on | C2 |
| `perf.leash` | – | 0 – 1 | 0 | C5 |
| `perf.variation` | – | 0 – 2 | 1 | C5 |
| `perf.density` | – | 0 – 1 | 0.5 | C5 |
| `perf.tempo_sync` | bool | off/on | off | C5 |
| `perf.sync_division` | enum | 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T | 1/16 | C5 |
| `perf.voice_bank` | enum | archetypes (+ banks added in F3) | archetypes | C5 |
| `perf.polyphony` | voices | 1 – 16 | 1 | C5 |
| `perf.seed` | int | 0 – 9999 | 0 (0 = random per note) | C5 |
| `perf.freeze` | bool | off/on | off | C5 |
| `out.stereo_spread` | – | 0 – 1 | 0.3 | C5 |
| `out.master_db` | dB | −48 – 6 | −6 | C1 |

The Perform page's 8 main macros: `perf.leash`, `perf.motion`, `perf.range`, `perf.speed`, `perf.variation`, `perf.breath`, `perf.snap`, `body.size`.

---

## 8. Rates and oversampling policy

See `01_research.md` §6.1. Summary: internal rate ≥ 176.4 kHz via power-of-two oversampling; the entire voice runs at the internal rate; controls interpolated linearly; half-band decimation with ≥ 90 dB stopband; latency reported.

---

## 9. File formats

All JSON formats carry `"format"` and `"version"` fields and have a JSON schema in `schemas/`. Python validates with `jsonschema`; C++ loaders check format/version and fail with a clear error message.

### 9.1 Audio

- Listening renders: WAV, 48 kHz, float32 (or PCM 24-bit), mono unless stereo matters.
- Dataset audio: keep the original file untouched in `data/raw/`; processed clips in `data/interim/` at the original sample rate (resampling only if < 32 kHz is unusable — document).

### 9.2 Render spec (`*.render.json`) — used from A1

```json
{
  "format": "cuicui.render_spec", "version": 1,
  "sample_rate": 48000, "duration_s": 2.0, "seed": 1, "oversampling": "auto",
  "params": { "src.L.beta": 0.3, "trachea.length_mm": 33 },
  "automation": {
    "resp.pressure": [[0.0, 0.0], [0.05, 0.8], [1.8, 0.8], [1.9, 0.0]],
    "src.L.beta":    [[0.0, 0.1], [2.0, 0.6]]
  },
  "automation_interp": "linear",
  "output": { "normalize_peak_dbfs": null }
}
```

`automation` values are `[time_s, value]` breakpoints. Unknown parameter IDs are an error.

### 9.3 Atlas (`assets/atlas/source_atlas_v1.*`) — from C2

- `source_atlas_v1.json`: metadata (grid definitions, γ₀, internal rate used, integration settings, generation date, git hash, field list).
- `source_atlas_v1.bin`: little-endian float32 arrays in the order listed in the JSON (`nu`, `amp`, `sci`, `regime`, `alpha_on`, `alpha_off`, inverse tables `beta_of_alpha_nu`, `alpha_lo_of_nu`, `alpha_hi_of_nu`).
- `core/include/cuicui/generated/atlas_tables.hpp`: generated from the binary by `scripts/gen_atlas_header.py` (constexpr arrays), so the plugin needs no file access. Size budget: ≤ 5 MB binary.

### 9.4 Gesture score (`*.gscore.json`) — from C4

A score can contain dense **physiological tracks** (layer 1, e.g. output of fitting), **gesture events** (musical layer, e.g. archetypes or voice-bank gestures), or both.

```json
{
  "format": "cuicui.gesture_score", "version": 1,
  "name": "zf_bird01_motif_003",
  "created_by": "WP-E3", "created_at": "2027-01-15T10:00:00Z",
  "settings": { "body.size": 1.0, "trachea.length_mm": 33.0, "trachea.reflection": 0.4 },
  "physio": {
    "control_rate_hz": 1000,
    "t0_s": 0.0,
    "tracks": {
      "resp.pressure": [0.0, 0.01, 0.03],
      "src.L.beta": [0.12, 0.12, 0.13],
      "src.L.gate": [1, 1, 1],
      "oec.freq_hz": [2900, 2910, 2930]
    }
  },
  "gestures": [
    {
      "t_s": 0.120, "duration_ms": 80, "type": "archetype:downsweep",
      "anchor_hz": 1800.0,
      "params": { "depth_cents": -900, "curvature": 0.5 },
      "hints": { "oec_ratio": null, "gape": null, "second_voice_cents": null },
      "curves": {
        "samples": 64,
        "pitch_cents": [0, -5, "... 64 values"],
        "amp_db": [-20, -6, "..."],
        "tone": [0.7, 0.7, "..."],
        "side": [-1, -1, "..."]
      }
    }
  ],
  "segments": [ { "t0_s": 0.10, "t1_s": 0.25, "label": "A" } ]
}
```

Rules: all `tracks` arrays have equal length; missing tracks use settings/defaults; `curves` are on normalised time u ∈ [0,1]; `pitch_cents` relative to `anchor_hz`; `amp_db` relative to gesture peak (≤ 0); `tone` ∈ [0,1]; `side` ∈ [−1, 1] (−1 = left). Optional static `hints` (null = controller default): `oec_ratio` (OEC resonance as a multiple of f₀, e.g. 5 for a harmonic stack), `gape` (0–1), `second_voice_cents` (if set, the other side sings at this interval from the main pitch → two-voice note). The example arrays above are abbreviated (`"..."`); real files contain exactly `samples` numbers.

### 9.5 Voice bank (`assets/voicebanks/<name>/bank.json`) — from F2

```json
{
  "format": "cuicui.voice_bank", "version": 1,
  "name": "zebra_finch_bird01",
  "description": "Learned from 3 recording sessions of bird01",
  "source": { "type": "fitted", "dataset_manifest": "data/manifest/zf_v1.json", "created_by": "WP-F2" },
  "settings": { "body.size": 1.0, "trachea.length_mm": 33.0 },
  "reference_pitch_hz": 620.0,
  "curve_samples": 64,
  "channels": ["pitch_cents", "amp_db", "tone", "side"],
  "gesture_types": [
    {
      "id": "g03", "label": "fast downsweep", "count": 212,
      "movement_score": 0.42,
      "duration_ms": { "dist": "lognormal", "mu": 3.9, "sigma": 0.25 },
      "anchor_offset_cents": { "mean": 350.0, "std": 80.0 },
      "mean": { "pitch_cents": ["64 values"], "amp_db": ["..."], "tone": ["..."], "side": ["..."] },
      "components": [
        { "variance": 1.0, "pitch_cents": ["64 values"], "amp_db": ["..."], "tone": ["..."], "side": ["..."] }
      ]
    }
  ],
  "syllable_types": [
    {
      "id": "A", "count": 140,
      "gesture_sequences": [ { "gestures": ["g03", "g07"], "prob": 0.8, "gaps_ms": [2.0] } ],
      "anchor_cents_rel_motif": { "mean": 0.0, "std": 40.0 },
      "example_scores": ["assets/voicebanks/zebra_finch_bird01/examples/A_0012.gscore.json"]
    }
  ],
  "grammar": {
    "type": "markov1",
    "states": ["START", "intro", "A", "B", "C", "END"],
    "transitions": [[0.0, 0.9, 0.1, 0.0, 0.0, 0.0]],
    "gap_ms": { "dist": "lognormal", "mu": 3.2, "sigma": 0.4 }
  },
  "couplings": { "amp_per_octave_db": 2.0, "oec_tracking": 0.6, "gape_tracking": 0.12 }
}
```

Generation rule: gesture curves = `mean` + Σₖ zₖ·`components[k]`, with zₖ ~ N(0, `variance`ₖ)·Variation (components stored in natural units, `variance` = variance of the coefficient).

### 9.6 Dataset and analysis files — from D1/D2

- `data/manifest/<dataset>.json`: list of recordings (path relative to `data/raw/`, bird id, session, sample rate, duration, channels, quality notes) and segments (file, t0, t1, label, type: intro/syllable/call/noise, annotator notes).
- Features: `data/features/<dataset>/<clip_id>.parquet` (one row per frame; columns documented in `python/cuicui/features/README.md`).
- Fits: `data/fits/<run_id>/<clip_id>.gscore.json` + `<clip_id>.fitlog.json` (loss history, settings, git hash, duration).

### 9.7 Presets — from C6

JUCE plugin state (XML via APVTS) + a human-readable JSON export `assets/presets/<name>.preset.json` (`"format": "cuicui.preset"`) listing parameter IDs and values, voice bank name, and gesture-page edits if any.

---

## 10. Testing strategy

| Kind | Tool | Rule |
|---|---|---|
| Unit tests (C++) | Catch2 in `core/tests` | every module; analytic checks where possible |
| Unit tests (Python) | pytest in `python/tests` | analysis, fitting utilities, schemas |
| Parity tests | pytest | C++ module vs slow numpy reference in `cuicui/reference` (B1–B3): max abs error < 1e-6 relative on 0.2 s renders |
| Golden renders | `core/tests/golden/*.render.json` + `expected.json` | compare **features** (f0, RMS, centroid, peak freqs) within tolerances, not raw samples (float differences across compilers) |
| RT safety | allocation-counting test build | 0 allocations inside `process()` after `prepare()` |
| Stability fuzz | C++ test | random parameter sweeps within registry ranges, 10 s total: 0 non-finite resets |
| Benchmarks | `bird_bench` | CPU % per voice at 48 kHz, reported in reports; no hard failure except > 2× regression vs `state.md` baseline |
| Plugin | pluginval (level 5 in C1, 8 in G1), auval | must pass |
| Listening | human, at the end of each WP | via listening page |

---

## 11. Listening kit conventions

- Output folder: `listening/<WP-ID>/` (not committed).
- Files: `NN_short_name.wav` (NN = 01, 02, …), 48 kHz. Peak-normalise to −3 dBFS **unless** the WP compares loudness (then say "not normalised" in the manifest).
- Manifest `listening/<WP-ID>/manifest.json`:

```json
{
  "format": "cuicui.listening_manifest", "version": 1,
  "wp": "B1",
  "title": "Labial oscillator — raw source",
  "items": [
    {
      "file": "01_hopf_onset.wav",
      "title": "Soft (Hopf) onset, β = 0.5",
      "listen_for": "Smooth fade-in of a nearly pure tone; no click.",
      "params": { "src.L.beta": 0.5 },
      "metrics": { "f0_hz": 2701, "onset_ms": 12.0 },
      "group": "onsets",
      "reference": null
    }
  ]
}
```

- `scripts/listen.sh <WP-ID>` → `listening/<WP-ID>/index.html` with, per item: title, audio player, spectrogram PNG (0–12 kHz, FFT 1024, hop 128, Hann, 80 dB range), waveform thumbnail for items < 0.5 s, "listen for", params, metrics. Items sharing `group` are shown side by side; `reference` (another file, e.g. the real recording) gets an A/B toggle.
- The report copies at most 10 small PNGs (≤ 300 KB each) into `agent documents/reports/assets/<WP-ID>/`.

---

## 12. Git and data policy

- Branch `main`; **one commit per approved WP**; commit message:
  ```
  WP-B1: labial source oscillator (normal form)

  <3–8 lines summary>
  Report: agent documents/reports/WP-B1_report.md
  ```
  Then tag `wp-B1`.
- `.gitignore` (created in A1) excludes: `build/`, `tools/`, `listening/`, `data/raw/`, `data/interim/`, `data/features/`, `data/fits/`, `.venv/`, `__pycache__/`, `.DS_Store`, `*.wav` outside `assets/` and `core/tests/golden/`.
- No file > 5 MB committed without a decision entry.
- Never commit secrets, personal data, or the raw zebra finch recordings.

---

## 13. Product naming (working defaults, see decisions.md)

| Item | Value |
|---|---|
| Manufacturer | Cuicui Instruments |
| Product (working title) | Syrinx |
| Manufacturer code / plugin code | `Cuic` / `Syrx` |
| Bundle ID | `com.cuicuiinstruments.syrinx` |
| CLAP ID | `com.cuicuiinstruments.syrinx` |

---

## 14. Glossary

| Term | Meaning |
|---|---|
| α (alpha) | oscillator unfolding parameter linked to air-sac pressure; sound for α above the onset boundary |
| β (beta) | oscillator unfolding parameter linked to labial tension; higher β → higher pitch |
| γ (gamma) | oscillator time scale; frequencies ∝ γ |
| ν (nu) | normalised frequency 2π f₀ / γ |
| Atlas | precomputed table of oscillator (or voice) behaviour over parameter grids |
| Archetype | hand-designed parametric gesture generator (whistle, trill, …) |
| Gesture | short smooth segment of pitch/amplitude/tone/side curves |
| Syllable | sequence of gestures; the unit annotated in zebra finch song |
| Motif | stereotyped sequence of syllables |
| Voice bank | learned gesture types + syllable grammar + couplings for one bird/species |
| Gesture score | file with physiological tracks and/or gesture events |
| Leash | macro blending player-controlled pitch and bird-generated pitch |
| Tone | position between rich (near threshold) and pure (far from threshold) at fixed pitch |
| OEC | oropharyngeal–esophageal cavity (Helmholtz resonator) |
| Onset boundary | α at which oscillation starts when ramping up from rest |
| SNIC | saddle-node on invariant circle bifurcation: onset with near-zero frequency, rich spectrum |
| Hopf | bifurcation with soft onset of a near-sinusoidal oscillation |
| Internal rate | oversampled rate at which the voice is computed |
| WP | work package (one agent task) |
