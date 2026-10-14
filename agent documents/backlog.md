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
- [C5] Factory MIDI map Leash → mod wheel (CC1) in C6 — the WP asked the human to map it in the DAW.
- [C5] `perf.voice_bank` = `learned` is a no-op until F3; keep two combo names so auval stays happy.
- [C5] Clamp generative syllable anchors to the InverseMap playable band so high Range does not fold.



- [R1] Per-bird harmonic levels. The bank stores the measured H2/H3 (22938: −32.7 / −34.2 dB over all syllables), but the voice uses D-022's −30 / −33 dB. When more birds arrive, let the voice take the bank's values, and check by listening.
- [R1] 28/123 syllables carry a `hop` flag (> 300 c in 1 ms, probably tracker voice hops). Re-track them with a two-voice tracker, or let the user drop flagged syllables.
- [R1] Room is on/off only. A room amount (or a different room per bird) would be a small R4 addition.
- [R1] "Now singing" display: a lock-free request FIFO from the audio thread to the editor (label, note, Hz), like the prototype's pills.
- [R1] One implementation for the web prototype and the plugin: compile the core `SyllableEngine` to WebAssembly so PROTO1 plays the C++ code itself.
- [R1] Several banks: a bank parameter and/or user bank files, loaded off the audio thread and swapped atomically (`SyllableEngine::setBank` already silences the voices).
- [R1] `.claude/launch.json` (a static server on `listening/`, for the in-app browser) was used during R1 and not committed. Add it if agents keep needing it.
- [R4] Host parameter names to match the UI ("Snap" → "Snap to note", "Polyphony" → "Birds", "Master" → "Volume"): a registry contract change; worth doing before a DAW-focused release (G1).
- [R4] The live display costs about 8–10 % of one core on the message thread while four birds sing (2 % at rest; Apple Silicon, Retina, 30 fps). Drawing the song into a scrolling image (only the newest few ms per frame) would cut most of it.
- [R4] Mark the "follow" sweet range (C4–C6) on the on-screen keyboard when Follow keys is chosen.
- [R4] Preset menu: show each preset's description in the menu; rename and delete your own presets from the plugin (today: in the Finder, "Show the presets folder").
- [R4] When R3 adds banks: a bird (bank) selector in the header, and a Variation knob in the Song card (the layout leaves room).
- [R4] MIDI learn (C6) was left out by the human (DAWs map controllers); the mod wheel drives "Snap to note".
- [R4] Editor timer: 30 Hz even at rest (2 % of a core). Dropping to ~10 Hz when nothing sings would halve it.
- [R4] The Standalone's automatic MIDI switch-on was not tried with a real MIDI device in the session (none connected); check it on first use, fallback Options > Audio/MIDI Settings.
- [R4] D-030 release call: a switch and a "call after" time on the Play page (today always on, 0.6 s fixed). A chord released together gives one call per bird; a "one call per release" option may be wanted.
- [R4] The PROTO1 web prototype and the Python reference (`cuicui.reference.singing`) do not have the D-030 release call; the reference comparisons run with it off. Port it if the prototype is used again.
- [R5] Tapped tunes (one random syllable per key) are recognised only on some draws (MELODY3: 2 of 4 answered screens), although every note is right on its own (TUNE1). Candidate tap strategies to test blind: keep the same syllable while a line of taps goes on; draw taps from narrow syllables only (loud part ≤ 5 semitones: 42 of 118). Held keys with "Finch, melody" are the route for tunes today (D-033).
- [R5] "Taps never start with a very wide sweep" (loud part > 8 semitones, 13 syllables) was not adopted (MELODY3: +4.8 points, better on 2 of 4 screens). The D-032 flag `no_tap` can do it in one bank-builder rule if it is wanted later.
- [R5] Prototype held-key options not picked (D-033): (a) the phrase goes on only after a hold threshold (0.4 / 0.5 s), (b) no closing call when the next key follows within 150 ms. Renders and numbers: WP-R5 report §6.
- [R5] With Birds 1, a release call (D-030) can overlap the next key's first syllable: a call is not a singer, so the polyphony limit does not cancel it. Cancel pending calls when a new key takes over in mono?
- [R5] TUNE1 page: the check round did not record plays per syllable; add it if the page is reused (e.g. for R3's recordings).
- [R5] The note-rule choices "steady parts", "landing" and "average" changed slightly with the −20 dB floor (median 20 c). Re-check by ear whether those three options are still needed now that the default matches the ear (TUNE1: E 98 %, D 85 %, average 81 %, landing 49 %).
- [R5] Item 11 (Frère Jacques held, "Finch, melody") ends high to the human's ear. Its last syllable is #97 (segment 115), whose TUNE1 note was moved +250 c in the first pass but +65 c in the check round. Re-check it by ear, and set it in `assets/voicebanks/zf_22938/note_corrections.json` (one line, then rebuild the bank).
- [R3] The long final calls ("alarm", song endings) sound thinner than the bird (INGEST1, the human: "the orginal sound in those case feels richer, denser, to have more low frequencies… i suspec another mechanism of production"). Investigate a second voice / denser source for them; the 3 calls of 22937 rejected in INGEST1 (segments 515, 547, 594) can come back then.
- [R3] New songs (new order, a Markov chain over syllable types) rated 79 vs 88.5 for the real songs in VARY1 (one screen better than real, one worse); the human chose to leave it out (2a). If taken up again: recombine only at the branch points the bird itself uses, and re-test. Code: `cuicui.variation.Model.new_phrase`.
- [R3] The new syllables' notes (hf_22938 after 53 s, all of hf_22937) are rule E only, never checked by ear. A TUNE1-style round on them (`scripts/studies/tune1_make.py`) would catch out-of-tune ones.
- [R3] The new syllables' labels are proposals (nearest human-labelled 22938 syllable; swipe/peep right about 2 in 3). A labelling round by ear, if labels start to matter (R6 queries by family).
- [R3] A bird selector (one bird alone, or both) in the header: the banks are separate files and `SyrinxAudioProcessor::useBankFiles` already loads any set; needs a parameter or a non-automatable setting, and the state.
- [R3] VARY1 comment (the human): "the quality of the bird sound is not catastrophic but sometimes misses a bit of timbre complexity" — with INGEST1's note on the long calls, a timbre step for the whistle (second voice, denser source) is worth planning.
