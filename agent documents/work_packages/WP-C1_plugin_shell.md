# WP-C1 — Plugin shell (VST3 / AU / CLAP / Standalone) + offline plugin renderer

| Field | Value |
|---|---|
| Phase | C — Playability |
| Depends on | B6 |
| Size | L |
| Milestone | – |

## 1. Goal

Wrap the complete voice into a real plugin that loads in DAWs, receives MIDI, exposes all registered parameters, reports latency, saves/restores its state and passes validation. Add `plugin_render`, a command-line tool that runs the plugin processor offline with a MIDI file and parameter automation — every later playability WP uses it to produce listening examples. Playing in this WP is still "physiological" (crude tuning); in-tune pitch-led playing is C3.

## 2. Read first

- `01_research.md` §6.1–§6.3, §7.2
- `02_architecture.md` §1, §3, §4, §6, §7 (all), §10, §13
- Report of WP-B6

## 3. Deliverables

| Path | Content |
|---|---|
| `cmake/Dependencies.cmake` | JUCE 8.x pinned tag; `clap-juce-extensions` pinned (unless native CLAP in the pinned JUCE — check and record in a decision) |
| `plugin/CMakeLists.txt` | `juce_add_plugin(Syrinx …)` formats VST3, AU, Standalone (+ CLAP); company/codes/bundle from `02_architecture.md` §13; `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE` |
| `plugin/Source/PluginProcessor.h/.cpp` | processor |
| `plugin/Source/ParameterLayout.h/.cpp` | APVTS layout generated from `params.def` |
| `plugin/Source/PluginEditor.h/.cpp` | temporary editor: generic parameter controls grouped by module + telemetry readout |
| `plugin/Source/SafetyLimiter.h` | transparent soft clipper at the output |
| `apps/plugin_render/main.cpp` | offline renderer through the processor |
| `plugin/tests/test_processor.cpp` | JUCE-linked Catch2 tests |
| `scripts/build.sh`, `scripts/check.sh` | build plugin targets; `check.sh` includes plugin build + processor tests |
| `scripts/validate_plugin.sh`, `scripts/get_pluginval.sh` | validation |

## 4. Specification

### 4.1 Parameters

- APVTS parameters created by iterating the registry: `juce::ParameterID{id, versionHint}` with `versionHint` = phase number of `sinceWP` (A=1, B=2, C=3, …) so later additions are host-compatible.
- Float params with `NormalisableRange` matching registry min/max/skew; enums as `AudioParameterChoice`; bools as `AudioParameterBool`.
- Add `out.master_db` (registry, Performance layer).
- Parameter smoothing is done by the voice/registry smoothers, not by JUCE.

### 4.2 Processor

- `prepareToPlay`: `ProcessSpec{sampleRate, samplesPerBlock, oversampling param}` → `BirdVoice::prepare`; `setLatencySamples(voice.latencySamples())`.
- `processBlock`: split the host block into sub-blocks of ≤ 32 samples; at each sub-block boundary read parameter values (atomic raw values) and MIDI events at their sample positions; build `ControlFrame` from/to; call `voice.process`.
- **MIDI in this WP (physiological playing, mono, last-note priority):**
  - note-on: `resp.pressure` target = 0.35 + 0.55·(velocity/127) (smoothed by respiratory dynamics); **crude tuning**: $\beta = (2\pi f_{note}/\gamma)^2$ (Hopf approximation, clamped to registry range) — good enough to feel pitch; overrides `src.L.beta` while a note is held.
  - note-off: pressure target 0 (release), β holds.
  - legato: overlapping notes change β without re-triggering pressure.
  - pitch bend ±2 semitones on the note frequency.
- Output: mono voice → stereo (both channels) → `out.gain_db` (already in voice) → `out.master_db` → `SafetyLimiter` (unity below −1 dBFS, smooth tanh-style knee above, never exceeds 0 dBFS).
- Telemetry: copied to atomics after each block for the editor.
- `getStateInformation`/`setStateInformation`: APVTS XML with a `stateVersion` attribute.
- No allocations, locks or logging in `processBlock` (verify with the allocation counter in a processor test).

### 4.3 Editor (temporary)

- Groups: Breath, Source L, Source R, Trachea, OEC & Beak, Noise, Body & Output, Engine — using standard JUCE sliders with `SliderAttachment`s.
- Telemetry panel (30 Hz timer): f₀ estimate, α_L/β_L, α_R/β_R, pressure, level, non-finite resets.
- Resizable; no custom graphics yet (C6).

### 4.4 `plugin_render`

```bash
plugin_render --midi phrase.mid [--params params.json] [--automation automation.json] \
              --sr 48000 --block 256 [--tail 1.5] [--seed 1] -o out.wav
```

- Hosts `SyrinxAudioProcessor` directly (no GUI), sends MIDI events at sample-accurate positions, applies parameter automation (JSON `{param_id: [[t, value], …]}` in plain units) at sub-block boundaries, writes stereo float WAV.
- Deterministic for the same inputs (seed sets `engine.seed`).
- Also accepts `--notes "60:0.0:1.0:100,64:1.0:0.5:90"` (note:start_s:dur_s:velocity) for quick tests without a MIDI file.

### 4.5 Validation

- `scripts/validate_plugin.sh 5`: pluginval strictness 5 on VST3 and AU; `auval -v aumu Syrx Cuic` for AU (after copying the component to `~/Library/Audio/Plug-Ins/Components` — the script does it and prints the path).
- If a CLAP validator is readily available, run it; otherwise note it in the report.

## 5. Suggested steps

1. JUCE + plugin target building VST3/AU/Standalone; processor passes audio.
2. Registry → APVTS; state save/load.
3. MIDI handling, sub-block processing, latency, limiter.
4. Processor tests; allocation test.
5. `plugin_render`; validation scripts; CLAP.
6. Listening renders; human DAW checklist.

## 6. Out of scope

Inverse map / accurate tuning (C2), pitch-led controller, ornaments, macros (C3+), custom UI (C6), presets (C6), polyphony (C5).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | `scripts/build.sh release` builds VST3, AU, Standalone (and CLAP) on the dev Mac. |
| 2 | pluginval strictness 5 passes for VST3 and AU; `auval` passes. |
| 3 | Processor test: note-on → non-silent output within 50 ms; note-off → output < −60 dBFS within 500 ms; reported latency equals `BirdVoice::latencySamples()`. |
| 4 | State round trip: set 10 random parameters, save, load into a new instance → identical values. |
| 5 | Zero allocations in `processBlock` over 10 s of MIDI playing with parameter changes (allocation counter). |
| 6 | Output never exceeds 0 dBFS (limiter) in a fuzz test with extreme parameters; limiter is bit-transparent below −1 dBFS. |
| 7 | `plugin_render` is deterministic (two runs byte-identical) and its output matches `bird_render` for an equivalent note without MIDI features (same pressure/β automation) within −60 dB difference. |
| 8 | Block-size robustness: renders with host block sizes 32, 256, 1024 differ by < −60 dB (differences only from sub-block parameter reading). |
| 9 | CPU at 48 kHz / 256 samples ≤ 1.5× the B6 single-voice number. |
| 10 | `scripts/check.sh` passes (now including plugin build and processor tests). |

## 8. Listening & results

Render with `plugin_render` into `listening/C1/`:

| File | Content | Listen for |
|---|---|---|
| `01_midi_phrase_crude.wav` | a short MIDI phrase (8 notes, legato and detached) | notes start/stop physically; pitch roughly follows (not in tune yet — expected) |
| `02_velocity_layers.wav` | same note at velocities 30, 60, 90, 127 | louder and brighter with velocity; onset gets sharper |
| `03_oec_automation.wav` | sustained note, `oec.freq_hz` automated 800 → 6000 → 800 Hz | formant sweep via host automation, no zipper noise |
| `04_bend.wav` | note with pitch bend up/down | smooth bend |

Add a screenshot of the Standalone app / generic editor to the report.

**Human checklist in the final message** (ask for results):
1. Open the Standalone app, select a MIDI keyboard, play.
2. Load the AU (Logic) or VST3 (Ableton/Reaper/Bitwig) in a DAW, play and automate one parameter, save the project, reopen it.
3. Report any crash, silence or click.

## 9. Pitfalls

- AU requires `IS_SYNTH` + correct AU type (`aumu`) and codes; changing codes later breaks sessions.
- Do not call `getRawParameterValue` lookups by string in `processBlock` (cache pointers in `prepareToPlay`).
- JUCE's `MidiBuffer` iteration does not allocate; creating new buffers does.
- Standalone app on macOS may need microphone permission prompts disabled (no audio input needed: set input channels to 0).
