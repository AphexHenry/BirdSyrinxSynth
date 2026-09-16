# Syrinx

Working title for an anatomy-based songbird synthesizer plugin (VST3 / AU / CLAP) by **Cuicui Instruments**.
This repository currently contains the DSP core, a command-line renderer, and a listening-page kit. The plugin arrives later.

## Build

Requirements (macOS Apple Silicon): CMake ≥ 3.25, Ninja, a C++20 compiler, [uv](https://docs.astral.sh/uv/), git.

```bash
./scripts/doctor.sh          # check tools
./scripts/build.sh release   # C++ core + apps + Python env
./scripts/test.sh            # C++ (Catch2) + Python (pytest)
./scripts/check.sh           # release build, tests, ruff, clang-format
```

Render a JSON spec to WAV:

```bash
./build/release/bin/bird_render path/to/spec.render.json -o out.wav
```

Build a listening page (after putting `manifest.json` and WAVs in `listening/<WP-ID>/`):

```bash
./scripts/listen.sh A1
open listening/A1/index.html
```

## Documents

Project specs, work packages and the agent protocol live in [`agent documents/`](agent%20documents/README.md).
Start there (`protocol.md`, `state.md`) if you are continuing this work.
