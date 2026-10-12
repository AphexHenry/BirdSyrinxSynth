# WP-R4 — Perform UI and presets (revised C6)

| Field | Value |
|---|---|
| Phase | R · Depends on R1 · Size L |

**Goal.** A usable instrument page for the R1 voice and modes, replacing the C6 plan, which was built for the rejected archetype generator.
- Big controls: Melody (snap, also on the mod wheel), Height (own height / follow, size), voice (whistle ± bird harmonics; anatomical experimental), start / order, pause, room, polyphony.
- An on-screen keyboard, like the prototype's footer keyboard.
- A small live display of the sung syllable (label, note, pitch trace).
- Factory presets (e.g. "Finch, natural", "Finch, on the note", "Finch, follow the keyboard", "Small flock").

Reuse the relevant parts of `WP-C6_perform_ui_presets.md` (layout rules, preset I/O, no audio-thread loading). Its generator-specific controls (Leash, Density, Variation for archetypes) are dropped or remapped. Variation arrives with R3.

## Notes from WP-R1

- **Parameters.** D-025 lists the parameters. The editor has a plain "Bird (whistle voice)" group (`PluginEditor.cpp`, `isBirdControl`, playing order in `resized()`).
  - Enums are `AudioParameterChoice` with readable names; on/off controls are `AudioParameterBool`.
  - `perf.snap` + the mod wheel set the melody snap (they add). `out.master_db` is the volume.
- **Live display.** `SyrinxAudioProcessor::whistleTelemetry()` gives keys singing and syllables sounding. A "now singing" readout (label, note, Hz), like the prototype's pills, would need a small lock-free FIFO of `SyllableRequest`s to the UI. The request already carries the syllable index (label via `syllableBank().label(i)`) and the shift.
- **Keyboard.** The in-app keyboard now spans C2–C8 and shows C4–C7. In follow mode the useful keys are C4–C6 (bird at C7–C9).
- **Presets.** Each preset idea maps onto D-025 parameters:
  - "Finch, natural": first on note off, snap 0.
  - "On the note": snap 1.
  - "Follow the keyboard": mapping follow.
  - "Small flock": polyphony 4–6, spread 0.6.
- **Human, R1 listening (2026-10-12):** with a tapped melody, "follow the keyboard" "sounds like Frère Jacques more than [own height] by far". Own height stays the default (D-020). A visible mapping switch, or a melodic preset using "follow", is worth considering.
