# WP-C6 — Perform UI, Physiology page, presets v1

| Field | Value |
|---|---|
| Phase | C — Playability |
| Depends on | C5 |
| Size | L |
| Milestone | **M4 — singing instrument** |

## 1. Goal

Give musicians a real instrument interface: a **Perform page** with the 8 macros and a live view of what the bird does, a **Physiology page** for experts, a **Settings page**, **MIDI learn**, and a first set of **factory presets**. Clear, musician-friendly wording everywhere.

## 2. Read first

- `01_research.md` §7.2, §7.5
- `02_architecture.md` §7.2, §9.7, §13
- Reports of WP-C1 … WP-C5

## 3. Deliverables

| Path | Content |
|---|---|
| `plugin/Source/ui/LookAndFeel.*` | vector look & feel (no bitmap dependencies) |
| `plugin/Source/ui/PerformPage.*` | 8 macro knobs, secondary controls, pitch trace, level meter, voice-bank selector, seed/freeze |
| `plugin/Source/ui/PhysiologyPage.*` | grouped controls + live "driven" values |
| `plugin/Source/ui/SettingsPage.*` | MIDI mapping, bend range, polyphony, scale/root, sync, oversampling |
| `plugin/Source/ui/PresetBar.*` | preset browser (prev/next/menu/save) |
| `plugin/Source/Presets.*` | factory presets (BinaryData), user presets on disk, JSON export (§9.7) |
| `plugin/Source/MidiLearn.*` | right-click learn/clear, stored in plugin state |
| `plugin/Source/ParamHelp.h` | one-sentence help text per parameter (musician language) |
| `assets/presets/*.preset.json` | ≥ 16 factory presets |
| `schemas/preset.schema.json` | preset schema |
| `plugin/tests/test_presets.cpp`, `test_midi_learn.cpp` | tests |

## 4. Specification

### 4.1 Perform page

- Top: preset bar; voice bank selector (`perf.voice_bank`); seed field + freeze toggle.
- Centre: **8 large macro knobs** — Leash, Motion, Range, Speed, Variation, Breath, Snap, Size — with value readouts in musical units (e.g. Size "×1.5", Speed "×0.5", Range "×1.2").
- Secondary row (smaller): Tone, Physics, Density, Glide, Scale/Root, Sync (+division), Polyphony, Stereo spread, Master.
- Live view: **pitch trace** of the last 3 s (target pitch and measured f₀ per voice, over a piano-roll-like note grid), breath/level meter, a small indicator when pitch is folded/clamped (out of range), and the playable range of the current Size shown on the note grid.
- Tooltips from `ParamHelp.h`.

### 4.2 Physiology page

- Groups: Breath (pressure, attack, release), Source L, Source R (β, gate, α offset), Coupling, Noise (tension σ, bandwidth, aspiration), Trachea (length, reflection, loss, input gain), OEC & Beak (freq, damping, gape, beak damping), Output (radiation, gain), Body (size, γ).
- In Perform mode, driven parameters show their **live value** next to the control (greyed control + telemetry number); a banner explains "In Perform mode, the bird controls these; switch to Physiology mode to play them directly".

### 4.3 Threading

- UI reads telemetry via lock-free structures (atomics or a single-producer/single-consumer FIFO for the pitch trace); repaint ≤ 30 fps; no locks shared with the audio thread.

### 4.4 Presets

- Format: JUCE state + JSON export `assets/presets/<name>.preset.json` (`"format": "cuicui.preset"`, version, name, category, author, description, parameters {id: value}, voice_bank, midi_learn (optional)).
- Factory presets (≥ 16) in categories: **Tonal** (clear whistling bird), **Buzz & stacks** (zebra-finch-like textures), **Trills**, **Flock**, **Tiny birds**, **Giant birds**, **Theremin** (Leash 0, melodic), **Expert/Physiology** (physiology mode starting points). Each with a one-line description.
- User presets: `~/Library/Application Support/Cuicui Instruments/Syrinx/Presets/` (macOS) — create folder on demand (message thread only).
- Loading a preset never glitches audio: parameters change through smoothing; a voice bank change is applied at the next note-on.

### 4.5 MIDI learn

- Right-click any macro → "Learn MIDI CC" / "Clear". CC mapping stored in plugin state and optionally in presets. Learned CC values act as absolute control of the macro.

## 5. Suggested steps

1. Look & feel and page skeleton with attachments.
2. Perform page incl. pitch trace (telemetry FIFO).
3. Physiology page with driven-value display; Settings page.
4. Presets system + factory presets (design them by ear with `plugin_render` renders; iterate).
5. MIDI learn; tests; screenshots; listening renders.

## 6. Out of scope

Gesture page (F4), learned voice banks (F3), installers/manual (G1).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | pluginval strictness 5 with editor open/close cycles passes (VST3 and AU). |
| 2 | Preset round trip: for each factory preset, load → export JSON → reload → all parameter values equal (±1e-6). |
| 3 | JSON presets validate against the schema. |
| 4 | MIDI learn mapping persists through state save/load. |
| 5 | Audio thread never blocks on UI: stress test (editor open, 60 s of rapid preset switching and knob automation) → no dropouts reported by a block-time monitor (worst block < 50 % of duration) and zero allocations in `processBlock`. |
| 6 | UI CPU: editor idle ≤ 5 % of one core on the dev Mac (report). |
| 7 | Every parameter visible in the UI has a help text (test iterates registry). |
| 8 | `scripts/check.sh` passes. |

## 8. Listening & results — milestone M4

- `listening/C6/`: each factory preset rendered with the same standard test (`examples/midi/preset_test.mid`: a held note 8 s, a short melody, a 3-note chord) → one file per preset, grouped by category, with the preset description as "listen for".
- Screenshots of the three pages (report assets).

Final message: **milestone M4**. Human checklist: install the plugin, browse presets, play with the 8 macros for 15 minutes, note: macros that do too little/too much, confusing labels, favourite and least favourite presets. Expect a feedback round on preset voicing.

## 9. Pitfalls

- Keep all UI text in English, short, no physics jargon on the Perform page.
- Don't load presets on the audio thread.
- Pitch trace must handle polyphony (one colour per active voice, max 8 traces).
