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
