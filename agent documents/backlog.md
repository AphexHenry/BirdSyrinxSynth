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
- [B4] Alarm “metallic” texture is ~15–20 Hz AM/FM (ring-mod throb), not broadband tension noise. Listening 06 now demos an 18 Hz LFO; a real gesture/LFO in the engine may belong in C4/C5.
- [B5] Weak coupling (ε ≤ 0.1) barely locks (Δβ ≤ 0.01). If two-voice locking is musically wanted, revisit ε scale or the coupling term.
- [B5] One shared α cannot span 1.2–6 kHz (09 topped out at ~4.56 kHz). C3 may need per-side α offsets for wide jumps/sweeps.
- [B6] One voice is ~2.7 % of a core at 48 kHz OS 4 (planning target 0.5 %). Candidates: skip RK4 on a gated-off side; UVT matrix only when params change; Instruments profile of `processLabial`. Human decides before any optimisation.
- [B6] For σ ≲ 0.35, request OS 8 (~4 cents at auto OS 4). Do not tie auto OS to Size (D-015).
- [C1] No `clap-info` on PATH; CLAP builds but is not validated. Install a CLAP validator before G1.
- [C1] `engine.seed` as `AudioParameterInt` up to 2³¹−1: auval warns the max does not stick (int32 display). Cap the host range (e.g. 0–1e9) in C6/G1.
- [C1] Processor tests `_Exit` after Catch2 because `alloc_counter.hpp` global `new`/`delete` aborts inside `DeletedAtShutdown::deleteAll()`. A JUCE-safe alloc probe would let the test binary shut down normally.
- [C2] Recover C6 (one semitone below the D6 floor at σ=1) with a denser β-grid near SNIC/onset — current 0.005 step cannot invert that edge to 5 cents.
- [C2] Continuous Tone morph at fixed ν pulls ~15–26 cents (listening 04) while held notes stay ≤ 0.3 cents; C3 may want a slow f₀ lock or denser inverse along α.
- [C3] In-app keyboard is D6–C8 (concert pitch in the σ=1 playable band). A transposing “bird octave” overlay could help pianists who expect C4.
- [C3] MIDI 60–72 with range_fold maps several piano octaves onto the same bird notes; glide tests must use in-range pitches (D6–C8) or the interval collapses.
- [C3] Jumps at the D6 playable floor sound printer-like (SNIC/onset noise + physical side-open). Keep demos in the middle of the band until C4/C5 add syllable-shaped breath around switches.
- [C4] High-depth upsweeps/switch_sweeps can leave the D6–C8 playable band and fold or sit on the floor — C5 should keep generated anchors inside the atlas range.



