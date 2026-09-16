# WP-A1 — Repository, build system, render CLI, listening kit

| Field | Value |
|---|---|
| Phase | A — Foundations |
| Depends on | – |
| Size | L |
| Milestone | – |

## 1. Goal

Create the project skeleton so that every later WP can: build the C++ core and apps with one command, run C++ and Python tests with one command, render a JSON "render spec" into a WAV file with a command-line tool, and produce a **listening page** (HTML with audio players and spectrograms) for the human. The only "sound" in this WP is a test tone generator used to prove the pipeline.

## 2. Read first

- `protocol.md` (all)
- `02_architecture.md` §1–§5, §9.2, §10, §11, §12, §13

## 3. Deliverables

| Path | Content |
|---|---|
| `.gitignore`, `.clang-format`, `README.md` (project root, short: what it is, how to build, pointer to `agent documents/`) | – |
| `CMakeLists.txt`, `CMakePresets.json` (`debug`, `release`, Ninja, output in `build/<preset>`) | top-level build |
| `cmake/Dependencies.cmake` | FetchContent: Catch2 v3, nlohmann_json (pinned tags) |
| `core/include/cuicui/version.hpp` | version string, git hash injected by CMake |
| `core/include/cuicui/dsp/rng.hpp` | xoshiro128+ PRNG (seedable, `uniform()`, `gaussian()`) |
| `core/include/cuicui/io/wav_writer.hpp` | minimal WAV writer: float32 (format 3) and PCM24, mono/stereo (offline only) |
| `core/include/cuicui/io/render_spec.hpp` | parse render spec JSON (§9.2) into a struct; breakpoint automation evaluation (linear) |
| `core/include/cuicui/model/test_tone.hpp` | `TestTone` generator: sine with `test.freq_hz` and `test.gain_db` automation (phase-continuous) |
| `apps/bird_render/main.cpp` | `bird_render <spec.render.json> -o <out.wav> [--seed N]` |
| `core/tests/test_*.cpp` | unit tests (see §7) |
| `core/tests/golden/test_tone_sweep.render.json` | example spec |
| `schemas/render_spec.schema.json`, `schemas/listening_manifest.schema.json` | JSON schemas |
| `python/pyproject.toml` | uv project `cuicui` (Python ≥ 3.11; deps: numpy, scipy, soundfile, matplotlib, jsonschema, pytest, ruff) |
| `python/cuicui/listen/__init__.py`, `spectrogram.py`, `manifest.py`, `page.py`, `__main__.py` | listening kit |
| `python/tests/test_listen_*.py` | tests |
| `scripts/doctor.sh`, `build.sh`, `test.sh`, `check.sh`, `listen.sh` | standard scripts (§4 of architecture) |

Note: in this WP the only "parameters" are `test.freq_hz` and `test.gain_db`, handled directly in `render_spec.hpp`. The real parameter registry arrives in A2.

## 4. Specification

### 4.1 Build

- CMake ≥ 3.25, C++20, Ninja. Targets: `cuicui_core` (INTERFACE library), `bird_render`, `cuicui_core_tests` (Catch2, registered with CTest).
- Warnings: `-Wall -Wextra -Wpedantic`; `-Werror` for `core/` and `apps/` in the `release` preset used by `check.sh`.
- The git hash is injected into `version.hpp` via `configure_file` (fallback "nogit").

### 4.2 Render spec handling

- Parse with nlohmann/json; validate `format == "cuicui.render_spec"` and `version == 1`.
- Automation: breakpoints `[t, v]` sorted by t; value before first = first value; after last = last value; linear interpolation. Unit test with edge cases.
- Unknown parameter IDs → error exit code 2 with message listing known IDs.

### 4.3 Test tone

- `TestTone::prepare(sampleRate)`, `reset()`, `process(float* out, int n, const Automation& freq, const Automation& gainDb, double t0)`.
- Phase accumulator in `double`; frequency evaluated per sample.

### 4.4 WAV writer

- RIFF/WAVE, `fmt ` chunk with format 3 (IEEE float, 32-bit) or 1 (PCM 24-bit), little-endian, correct chunk sizes. Must be readable by Python `soundfile` (test in Python).

### 4.5 Listening kit (Python)

- `spectrogram.py`: `save_spectrogram(wav_path, png_path, fmax=12000, n_fft=1024, hop=128, db_range=80)`; also `save_waveform(...)` for short clips.
- `manifest.py`: load + validate against `schemas/listening_manifest.schema.json` (structure in `02_architecture.md` §11).
- `page.py`: generate `index.html` next to the manifest: title, WP id, generation time, git hash; for each item: title, `<audio controls src="...">`, spectrogram image, waveform image if duration < 0.5 s, "listen for", params table, metrics table. Items with the same `group` in a flex row. If `reference` is set: two players side by side labelled "A: this" / "B: reference". No external CDN, no JavaScript framework (plain HTML/CSS, a few lines of JS allowed).
- CLI: `uv run python -m cuicui.listen page listening/<WP-ID>/manifest.json`.
- `scripts/listen.sh <WP-ID>` wraps it (run from project root; `cd python` handled inside).

### 4.6 Scripts

- `doctor.sh`: checks `cmake --version` ≥ 3.25, `ninja`, a C++20 compiler (`c++ --version`), `uv`, `git`, Xcode (`xcode-select -p`, warn only), and prints a table OK/MISSING with install hints (`brew install …`).
- `build.sh [debug|release]` (default release): `cmake --preset <p>` + `cmake --build --preset <p>`; also `cd python && uv sync` (creates `.venv`).
- `test.sh`: `ctest --preset release --output-on-failure` + `cd python && uv run pytest -q`.
- `check.sh`: build release + tests + `uv run ruff check` + `clang-format --dry-run --Werror` on `core/` and `apps/` if `clang-format` exists (warn if missing). Final line: `CHECK PASSED` or `CHECK FAILED: <step>`.

## 5. Suggested steps

1. `git init` if needed; `.gitignore` per `02_architecture.md` §12; first scripts (`doctor.sh`).
2. CMake skeleton + Catch2 + one trivial test; `build.sh`, `test.sh`.
3. PRNG, WAV writer, render spec parser, test tone + tests.
4. `bird_render` CLI.
5. Python project with uv; listening kit + tests.
6. `check.sh`; listening page for this WP.

## 6. Out of scope

JUCE, plugin, parameter registry, oversampling, Python bindings (A2), any bird model.

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | `scripts/doctor.sh` runs and reports the environment (all required tools OK on the dev Mac, or the missing ones listed with install commands and flagged to the human). |
| 2 | From a clean state (`rm -rf build`), `scripts/build.sh release` succeeds. |
| 3 | `scripts/test.sh` passes; at least: automation interpolation edge cases, PRNG reproducibility (same seed → same sequence; different seeds differ), WAV header correctness (chunk sizes), render spec errors (bad format/unknown ID). |
| 4 | Test tone accuracy: a 1000 Hz render (48 kHz, 1 s) measured by FFT peak interpolation or zero crossings is within ±0.1 Hz. |
| 5 | Determinism: rendering the same spec twice gives byte-identical WAV files (compare SHA-256 in a test or script). |
| 6 | Python: `soundfile` reads the rendered WAV (float32 and PCM24) with correct sample rate, length and values (max abs diff < 1e-6 for float). |
| 7 | Listening page generated for this WP and opens in a browser with working audio players and spectrograms. |
| 8 | `scripts/check.sh` prints `CHECK PASSED`. |
| 9 | `git status` after staging shows no files from `build/`, `listening/`, `.venv/`. |

## 8. Listening & results

Render into `listening/A1/`:

| File | Content | Listen for |
|---|---|---|
| `01_sweep_log_100_12k.wav` | 4 s logarithmic sine sweep 100 Hz → 12 kHz, −12 dBFS | clean sweep; spectrogram shows one thin rising line, no artefacts |
| `02_tone_1k_gain_ramp.wav` | 2 s 1 kHz tone, gain −40 → 0 dB → −40 dB | smooth level change, no clicks |
| `03_two_seeds_noise.wav` | 1 s white noise (seed 1) then 1 s (seed 2) — uses PRNG | two different noise bursts (proves seeding works) |

The report should show the spectrogram of item 01 and the determinism check output.

## 9. Pitfalls

- The folder `agent documents` contains a space: quote it in scripts.
- Apple Silicon Homebrew lives in `/opt/homebrew/bin`; make scripts robust to PATH.
- `uv sync` needs a `[build-system]`-less project for now (A2 adds scikit-build-core); keep `pyproject.toml` simple here.
- Do not add JUCE yet (download is large and slows every build).
