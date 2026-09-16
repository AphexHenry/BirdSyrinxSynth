# backlog.md — ideas for later

> Agents append one line per idea: `- [WP-<ID>] <idea> — <why it could matter>`. The human reviews this list at milestones and turns items into WPs when wanted.

## Seeded during planning

- [planning] Voice banks from other songbirds (canary, nightingale, blackbird, thrushes) using the same fitting pipeline — widens the musical palette beyond zebra finch.
- [planning] Other bird families (doves: closed-beak coos with esophageal inflation; parrots: tongue articulation; non-vocal sounds) — explicitly deferred by the human (D-008).
- [planning] Audio input "follow" mode: sing/whistle into the plugin (sidechain), pitch-track it and drive the bird's pitch/breath — very natural for musicians.
- [planning] MPE advanced mapping and per-note expression presets.
- [planning] Microtuning support (MTS-ESP, Scala files).
- [planning] Neural generative model of gestures (e.g. small VAE) as an alternative to FPCA if the FPCA variations sound too tame.
- [planning] Differentiable JAX twin of Tier-1 and amortised inference (WP-G3, optional).
- [planning] Perceptual loss from a bird-sound model embedding (Perch / BirdNET) inside fitting, not only for evaluation.
- [planning] Flock mode spatialisation: distance filtering, early reflections, habitat reverbs (forest, reed bed, aviary).
- [planning] Morphing between two voice banks (e.g. zebra finch ↔ archetypes) with a single knob.
- [planning] Source–tract interaction (feedback of tract pressure on the labia) — may add realism in strongly resonant notes.
- [planning] Small tracheal length and beak-radiation directivity modulations — probably subtle.
- [planning] Windows build; AUv3 / iOS version.
- [planning] "Dawn chorus" generator using several banks and densities over long time spans.
- [A1] `clang-format` is missing on the dev Mac; `check.sh` currently warns and skips C++ format — install with `brew install clang-format` so later WPs get a real format gate.
- [A2] `Oversampling::decimate` assumes `nHost ≤ maxBlockSize` (scratch is sized in `prepare`). Callers must chunk, as `BirdVoice` does. A bounds check would make misuse fail loudly.
- [D1] Custom **tseg** JSON adapter (schema 1.2: SILENCE/NOISE/BIRD_SOUND + `type_label`) — recordings already exist; D1 parked until after Phase B.
- [B2] ℓ = 0 bypasses the one-pole rather than using the written 22 kHz cutoff — a 22 kHz pole at 192 kHz is ~1 dB down at 12 kHz and would break the analytic-magnitude test.
- [B3] Real 07/08 comparisons used `22938.wav` peep (7.828–7.994 s) and alarm (23.48–23.98 s). Human: 08 is metallic / fast-FM — chase with B4 tension noise.

