# decisions.md — decision log

> Append-only log. Agents add entries with status `proposed`; the human approves them together with the WP commit (then the agent sets `accepted`) or rejects them (`rejected`).
> Format: `D-<number> — <title>` · Status · Date · Origin · Context · Decision · Consequences.
> Statuses: `accepted`, `proposed`, `open` (needs the human), `rejected`, `superseded by D-xxx`.

---

### D-001 — Technology stack
- **Status:** accepted · 2026-09-16 · planning
- **Context:** final product is a plugin for musicians; same DSP must run in plugin, CLI, Python analysis and fitting.
- **Decision:** C++20 header-only core without JUCE; JUCE 8.x for the plugin (VST3, AU, Standalone, CLAP via clap-juce-extensions or native if available); CMake + Ninja; Catch2; Python ≥ 3.11 with uv; nanobind + scikit-build-core bindings; pycma for CMA-ES.
- **Consequences:** agents must keep `core/` free of JUCE includes; Python analysis always calls the real C++ engine.

### D-002 — Tier-1 anatomical model
- **Status:** accepted · 2026-09-16 · planning
- **Decision:** labial source = Takens–Bogdanov normal form (Mindlin group) with this project's sign convention (sound for α > 0, pitch rises with β); trachea = delayed-feedback comb with fractional delay; upper vocal tract = glottis–OEC–beak 3rd-order acoustic circuit; radiation = derivative of beak flow; respiratory drive and noise as designed in `01_research.md` §4.
- **Consequences:** Tier-2 (physical labial model) is optional (G2).

### D-003 — JUCE licence
- **Status:** open · 2026-09-16 · planning
- **Context:** JUCE is dual-licensed (open-source licence or commercial licence tiers). The terms affect whether the plugin can be sold/distributed closed-source.
- **Decision needed from the human** before any public release: check current JUCE licence terms and choose. Development can proceed meanwhile.

### D-004 — Product naming (working defaults)
- **Status:** accepted (provisional) · 2026-09-16 · planning
- **Decision:** manufacturer "Cuicui Instruments", product working title "Syrinx", codes `Cuic`/`Syrx`, bundle ID `com.cuicuiinstruments.syrinx`.
- **Consequences:** changing the plugin codes after release breaks saved sessions; final name to be confirmed before G1 (check trademark/name availability).

### D-005 — Oversampling and integration
- **Status:** accepted · 2026-09-16 · planning
- **Context:** quick simulation: RK4 at 44.1 kHz gives 10–13 cents pitch error; 2× < 1 cent; 4× converged.
- **Decision:** internal rate ≥ 176.4 kHz (power-of-two oversampling), RK4 for the oscillators, trapezoidal rule for the vocal tract circuit, whole voice at internal rate, half-band decimation.

### D-006 — Three control layers; gestures hidden by default
- **Status:** accepted · 2026-09-16 · human
- **Decision:** layers = performance (macros, MIDI) / gestures (advanced only) / physiology (experts). The engine finds likely gesture curves from data and generates variations at performance time; users do not edit gestures by default.

### D-007 — Pitch-led playability
- **Status:** accepted · 2026-09-16 · human
- **Decision:** pitch is the master curve; amplitude, tone, side, OEC and gape follow from it; physiology is solved through the atlas/inverse map. Macros: Leash, Motion, Range, Speed, Variation, Breath, Snap, Size. Musicians are the target users.

### D-008 — Scope: songbird syrinx; zebra finch data
- **Status:** accepted · 2026-09-16 · human
- **Decision:** only the songbird (oscine) syrinx is modelled. Other bird families are out of scope for now. Data-driven work uses the human's annotated zebra finch recordings.

### D-009 — Agent workflow
- **Status:** accepted · 2026-09-16 · human
- **Decision:** one WP per chat in Cursor; same start prompt ("do as instructed in protocol.md"); human listens at the end of each WP and approves the commit; the agent never commits without approval.

### D-010 — No existing scientific synthesizer project as reference
- **Status:** accepted · 2026-09-16 · human
- **Decision:** agents must not base the design on the existing scientific birdsong-synthesis project the human knows of (results judged unsatisfying). Published papers are fine as scientific references.

### D-011 — Data policy
- **Status:** accepted · 2026-09-16 · planning
- **Decision:** raw recordings, interim clips, features, fits and listening renders are never committed. Manifests, schemas, small assets (≤ 5 MB), reports and small images are committed.

### D-012 — Trachea reflection default
- **Status:** accepted (provisional) · 2026-09-16 · planning
- **Decision:** default `trachea.reflection` = +0.5 (quarter-wave behaviour). To be re-evaluated by listening in B2 and by the hypothesis test in E4.

### D-013 — Optional render-spec `generator` field (tone / noise)
- **Status:** rejected — replaced by `test.waveform` · 2026-09-16 · WP-A1
- **Context:** A1 must prove the PRNG with a two-seed noise burst. A proposed top-level `"generator"` field on the render spec was rejected: waveform is a test parameter, not a file-format field.
- **Decision:** use `params."test.waveform"` with values `sine` | `noise`, handled in `render_spec.hpp` like `test.freq_hz` / `test.gain_db`. A2 moves it into the registry and adds `saw`.
- **Consequences:** no top-level `generator` field. CLI `--format` (WAV encoding) is unrelated and stays.

### D-014 — Optional listening-manifest `figures` array
- **Status:** accepted · 2026-09-16 · WP-A2
- **Context:** WP-A2 must show the decimator magnitude response on the listening page. That plot is not an audio item.
- **Decision:** allow an optional top-level `figures` array on `cuicui.listening_manifest` v1: `{file, title, caption?}`. Items stay as they are.
- **Consequences:** `scripts/listen.sh` embeds those PNGs on the page. Additive; old manifests remain valid.

### D-015 — Auto oversampling does not follow body size
- **Status:** accepted · 2026-09-17 · WP-B6
- **Context:** At σ = 0.25, γ = 96 000. RK4 at OS 4 is ~4 cents sharp vs OS 8 (measured 10657 vs 10633 Hz). The WP pitfall said: if error exceeds 1 cent at OS 4, force a higher internal rate — but latency must stay constant for the host.
- **Decision:** Keep the existing auto-OS policy (internal rate ≥ 176.4 kHz, independent of `body.size`). Sessions that will use σ ≲ 0.35 should set `engine.oversampling` = 8 explicitly. Do not change OS when the musician moves Size.
- **Consequences:** Tiny-bird pitch at auto OS 4 is a few cents sharp. Plugin latency does not pump when Size is automated.

### D-016 — JUCE 8.0.15 pin; CLAP via clap-juce-extensions
- **Status:** accepted · 2026-09-17 · WP-C1
- **Context:** Architecture/D-001 specify JUCE 8.x. C1 must pin an exact tag and add CLAP. JUCE 8.0.15 (latest 8.x at session time) has no native CLAP. JUCE 9.0.2 exists but would change the stack contract.
- **Decision:** Pin JUCE `8.0.15` (no native CLAP) and `clap-juce-extensions` at commit `55525c9` (2026-09-12; tag 0.26.0 does not compile against JUCE 8.0.15). CLAP ID `com.cuicuiinstruments.syrinx`.
- **Consequences:** Plugin formats are VST3, AU, Standalone, and CLAP. A later move to JUCE 9 needs a new decision.

### D-017 — Additive C4 Python APIs and bird_render gesture scores
- **Status:** proposed · 2026-09-18 · WP-C4
- **Context:** WP-C4 adds the gesture-score file format (already specified in `02_architecture.md` §9.4) and Python/CLI entry points that §6.1 does not list yet.
- **Decision:** Additive, non-breaking APIs:
  - `cuicui_core.archetype_names()`, `archetype_params(name)`, `generate_archetype(name, params, seed)`, `render_score(score, sample_rate)`
  - `bird_render` accepts `*.gscore.json` (`format: cuicui.gesture_score`) as well as render specs; optional `--sr`
  - `GestureSample.side` uses NaN = unset (same convention as hint fields); when set, the controller uses equal-power gates from side instead of the jump switcher
- **Consequences:** Existing render-spec CLI and C3 MIDI paths are unchanged. C5 plugs in via `ScoreGestureSource` / `VoiceController::setGestureSource`.



### D-018 — Approach pivot: gestures come from real recordings; understand before building
- **Status:** accepted · 2026-10-11 · human (approach review R0)
- **Context:**
  - C4/C5 hand-designed gestures sounded like "morse code". The C5 output measured 16× slower than the real bird, with 60 % steady frames (6 % in the bird) and elements 2.5× too long.
  - Real gestures on a plain sine "really feel like a bird" (rated 94–100 vs the recording).
- **Decision:**
  - Bird identity comes from **real syllables of real recordings** (pitch and loudness curves); hand-designed archetypes are dropped.
  - **Real syllables first, variations later**, and variations stay within the bird's style.
  - Each open question is answered with a short blind listening test (the human as tester, pre-registered decision rule) before implementation.
- **Consequences:** C5's generator content, C6, D2, E1–E4 and F1–F4 are superseded or optional. The new phase is R (R1–R4). See `reviews/2026-10-11_approach_review.md`.

### D-019 — The note of a syllable
- **Status:** accepted · 2026-10-11 · human (after PITCH1/PITCH2)
- **Decision:**
  - A syllable's note = its **longest steady part** (pitch within ±50 c for ≥ 20 ms), otherwise its **ending** (last quarter).
  - "Steady parts", "landing" and "average" stay available as a user toggle.
- **Context:** pitch at 3–4 kHz is fuzzy (±1.5 semitones on steady tones, more on real syllables), and a held note 2–3 octaves below barely anchors the bird. The aim is "dance around the note", not "in key".

### D-020 — Keyboard mapping and placement
- **Status:** accepted · 2026-10-11 · human
- **Decision:**
  - **Own height** (default): a key's note name, sung at the octave nearest the bird's natural height.
  - Option **follow the keyboard**: the bird sings 3 octaves above the key, so melodies keep their shape.
  - The **first syllable lands exactly on the note**; following syllables are placed by a Melody/snap control (natural melody ↔ all on the note; mod wheel).
  - Size transposes the whole bird.

### D-021 — Playing modes A + B as one behaviour; modular for C, D, E
- **Status:** accepted · 2026-10-11 · human
- **Decision:**
  - Tap a key → one real syllable (**a random one** by default).
  - Hold → the bird continues the real phrase around the note, with real timing and pauses between phrases.
  - Release → the current syllable finishes.
  - The code must be modular (input → playing mode → syllable bank → placement → voice) so modes C (conductor/flock), D (drawn gestures) and E (whistle-in) can be added later.

### D-022 — Default voice: the whistle (non-anatomical), with the bird's own harmonics
- **Status:** accepted · 2026-10-11 · human ("if the anatomical model does not work, let's simply switch to what works, even if non-anatomical")
- **Context:**
  - COLOUR1/COLOUR2: a sine on the real gestures scored 94; with the bird's own harmonics (−30 / −33 dB, the level measured in 22938) it scored 98–100; breath noise hurt at every level; room had no consistent effect.
  - ENGINE1: the anatomical engine, even with the click fix, scored 100 on one passage and 52 on another; the whistle scored 100 on both.
- **Decision:**
  - The default voice is a **sine following the real contour + 2nd/3rd harmonics at −30 / −33 dB, no breath, light room** (optional).
  - The anatomical engine becomes an **optional/experimental** voice (WP-R2).
- **Consequences:** the anatomical model is no longer the product's core voice. Anatomical controls may survive as musical controls (size, brightness) mapped onto the whistle voice.

### D-023 — Recording 22938 and data
- **Status:** accepted · 2026-10-11 · human
- **Decision:** `22938` is a **zebra finch**. Its recorded repertoire (tonal sweeps at 2.5–4.9 kHz, no low harmonic stacks) sets the design targets, not the textbook song in `01_research.md` §2.7. Many more recordings are available; to be ingested when R3 needs them.

### D-024 — C5 committed as partial; the plan continues with R1
- **Status:** accepted · 2026-10-11 · human
- **Decision:**
  - C5 is committed as `partial` (commit `0822405`): its plumbing (allocator, polyphony, tempo, Leash blend) is reused; its generator content is replaced.
  - New order: **R0** (review and studies, awaiting approval) → **R1** (real syllables + whistle + modes A/B in the plugin) → **R4** (Perform UI and presets) → **R3** (variations).
  - R2 (anatomical voice) is optional.

### D-025 — WP-R1 parameters for the whistle voice and modes A + B
- **Status:** accepted · 2026-10-12 · WP-R1
- **Context:** WP-R1 §3.4 lists the controls of the whistle path; new parameter IDs need a decision (`02_architecture.md` §7).
- **Decision:**
  - New IDs (all `sinceWP` "R1", layer Performance; JUCE version hint 8 for "R"):

    | ID | Values | Default |
    |---|---|---|
    | `voice.type` | Whistle / Anatomical (experimental) | Whistle |
    | `voice.harmonics` | on/off (bird's 2nd/3rd harmonics, −30/−33 dB) | on |
    | `perf.note_rule` | longest steady part else ending / steady parts / landing / average | rule |
    | `perf.mapping` | own height / follow the keyboard | own height |
    | `perf.first_on_note` | on/off | on |
    | `perf.order` | random start / phrase start / shuffled | random start |
    | `perf.pause_s` | 0.1–2 s | 0.5 |
    | `perf.size_st` | −12…+12 semitones | 0 |
    | `out.room` | on/off | on |

  - Reused IDs:
    - `perf.snap` is the melody/snap control for the whistle; the mod wheel (CC1) adds to it.
    - `perf.polyphony` counts the keys that sing at once; **its default changes from 1 to 4** (prototype default).
    - `out.stereo_spread` sets the per-key pan range (±0.3 by default).
    - `out.master_db` is the volume.
    - `perf.seed`, or `engine.seed` when perf.seed = 0, seeds the random choices.
  - Velocity → gain 0.12 + 0.28·v^1.5; CC64 sustain.
  - On/off parameters are host booleans; float parameters display 1–3 decimals.
- **Consequences:**
  - The anatomical path keeps its parameters and is chosen with `voice.type`.
  - Tests written for the anatomical engine now select it explicitly (`plugin/tests/test_helpers.h`).

### D-026 — Syllable bank file format `cuicui.syllable_bank` v1
- **Status:** accepted · 2026-10-12 · WP-R1
- **Context:** R1 needs the bird's real syllables as an asset. The F2 voice-bank format (`02_architecture.md` §9.5) describes learned gesture models, not real syllables.
- **Decision:**
  - New format `cuicui.syllable_bank`, version 1 (`schemas/syllable_bank.schema.json`), stored at `assets/voicebanks/<name>/bank.json`. Contents:
    - phrases of syllables with real onsets (`t_ms`);
    - 1 ms `f_hz` / `a` curves;
    - per-syllable anchors for the four note rules, plus the default rule's span;
    - the bird's centre per rule;
    - build settings, stats and a content hash.
  - Built by `python -m cuicui.bank` (the PROTO1 recipe, species- and recording-agnostic).
  - The factory bank `zf_22938` (0.204 MB, hash `09b16ee69ac8`) is compiled into the plugin (JUCE BinaryData). C++ loads banks with `io::parseSyllableBank` / `loadSyllableBank`, never on the audio thread.
- **Consequences:**
  - §9.5 stays for learned models (F2).
  - More recordings (R3) only need their annotation files.

### D-027 — WP-R1 additive APIs, tools and latency policy
- **Status:** accepted · 2026-10-12 · WP-R1
- **Decision:**
  - **Core** (header-only, no JUCE):
    - `control/` syllable_bank, placement, phrase_order, playing_mode (interface for modes C/D/E), mode_ab, syllable_engine;
    - `model/whistle_voice`;
    - `dsp/` fft, room;
    - `io/syllable_bank_io`.
  - **Python:**
    - `cuicui_core.sing(bank_path, events, settings, sample_rate, duration_s, seed, block)` returns stereo audio and the syllable starts;
    - packages `cuicui.bank` (build / validate) and `cuicui.reference.singing` (Python port of the prototype logic and voice);
    - the R0 tracker moves to `cuicui.analysis.f0_track`; `scripts/review/track.py` re-exports it.
  - **`plugin_render`:** new option `--syllable-log out.json`.
  - **Latency:** the plugin keeps reporting the anatomical engine's latency (35 samples at 48 kHz) and delays the whistle by the same amount, so switching voices never changes the latency the host sees.
- **Consequences:** none for existing callers (all additive).

### D-028 — Preset file format `cuicui.preset` v1, factory and user presets
- **Status:** accepted · 2026-10-12 · WP-R4
- **Context:** `02_architecture.md` §9.7 names the preset format (from C6) without details. R4 needs factory presets, presets saved by the user and a plugin state that remembers the preset.
- **Decision:**
  - Format `cuicui.preset`, version 1 (`schemas/preset.schema.json`): `name` (required), `category`, `author`, `description`, `voice_bank`, `parameters` {registry ID: value}.
  - Values are in the parameter's own units. Choices are written by name (index also accepted); switches as true/false (0/1 accepted). Unknown IDs are ignored.
  - **Loading a preset sets every parameter:** listed ones to their value, all others to their default. A preset made before a parameter existed keeps its sound.
  - Factory presets: `assets/presets/NN_name.preset.json`, compiled in (JUCE BinaryData), listed in file order. `01_finch` equals the defaults and is the name of a new instance.
  - User presets: one file each in `~/Library/Application Support/Cuicui Instruments/Syrinx/Presets/` (created when the first one is saved). Saving writes every parameter.
  - Presets are read, parsed and applied on the message thread only (`PresetManager`); the audio thread sees parameter changes, as with automation.
  - Plugin state: new XML attributes `presetName`, `presetUser` and `uiPage` on the state root (additive; older sessions load as "Untitled").
- **Consequences:** §9.7 gains these details. The voice bank stays fixed (one bank) until R3 adds a bank choice; `voice_bank` is informative in v1.

### D-029 — WP-R4 additive APIs, editor architecture and tooling
- **Status:** accepted · 2026-10-12 · WP-R4
- **Decision:**
  - **Core:** `SyllableRequest` gains `note` (the MIDI key) and `targetCents` (that key's target pitch), filled by `ModeAB`. Additive; the R1 reference comparison is unchanged.
  - **Plugin:**
    - `SyllableFeed`: a lock-free FIFO of syllable starts plus the whistle clock and live values (mod wheel, pedal, external MIDI notes). Audio thread → editor; drops when full, never blocks.
    - `processWhistle` forwards the engine's per-call log to the feed and to `setSyllableLog` (whose behaviour is unchanged).
    - `PresetManager` (D-028); `uiPage` in the state.
  - **Editor:**
    - a fixed 1080 × 720 design, scaled as a whole (aspect locked, 0.75–1.5×);
    - pages Play and Advanced (the R1 rows of the anatomical controls, as they were);
    - one `SyrinxLookAndFeel`, vector only;
    - fonts: Avenir Next (built into macOS; other systems use their default sans-serif).
  - **Standalone:** on the first open, if no MIDI input is enabled, every MIDI input is switched on. `StandalonePluginHolder` is used only in the Standalone build.
  - **Display names in the UI** differ from three host parameter names:
    - "Snap to note" (`perf.snap`, host name "Snap");
    - "Birds" (`perf.polyphony`);
    - "Volume" (`out.master_db`).
    The registry is unchanged.
  - **Tools:**
    - `plugin_render --preset <name|file>`, `--screenshot-page play|advanced`, `--screenshot-scale <f>`. A `--screenshot` taken with `-o` now shows the end of the render.
    - Syllable logs gain `key` and `target_cents`.
    - `scripts/validate_plugin.sh [strictness] [--gui]` also runs pluginval's editor tests.
- **Consequences:** none for existing callers. Renaming the host parameter names to match the UI is a separate, later contract change (backlog).

### D-030 — The bird's long rising call ends a long key press, and only that
- **Status:** accepted · 2026-10-12 · the rule: human; the details: WP-R4 (feedback), approved with the commit
- **Context:**
  - In 22938 the "alarm" syllables (4 × "alarm", 1 × "alarm_grillon") are 4–5× longer than the others (0.42–0.52 s), and every one glides up about a third (≈ 3.2 → 3.9–4.1 kHz).
  - 4 of the 5 are followed by a pause, so to the ear they end phrases.
  - The human: "this alarm, i want it to be played only if the key has been pressed more than 600ms, and only on key release. it should not be triggered with a short press."
- **Decision:**
  - **Rule (human):** the call never starts or continues a phrase and never answers a tap or a short press. When a key that sang at least 0.6 s is released, the bird ends with one call.
  - **Details (proposed):**
    - Which syllables: labels starting with "alarm" (`kReleaseLabelPrefix`), one chosen at random per release from the key's own random stream.
    - When: after the syllable still sounding, plus the bird's own lead-in silence (the median gap before its calls, 60 ms in 22938, `SyllableBank::releaseGapMs`), or at once if the bird was between phrases.
    - Placed like a following syllable (snap to note + mod wheel), with the key's loudness and stereo place.
    - Pedal: the press lasts until the pedal is lifted.
    - Pressing the same key again before the call starts cancels it. Keys released by the polyphony limit, all-notes-off or a voice switch never call.
    - Phrases close up over a skipped call: the next syllable starts as much earlier as the call lasted, so the silences around it remain.
    - On by default; `PlayContext::releaseAlarm = false` (`plugin_render --no-release-alarm`, `sing(..., {"release_alarm": 0})`) gives the R1 / PROTO1 playing for the reference comparisons. Not a host parameter.
- **Consequences:**
  - The plugin no longer plays exactly as the PROTO1 web prototype, which keeps the calls inside phrases. The R1 equivalence tests now run with the rule off.
  - The 0.6 s threshold is fixed (a control is in the backlog).

### D-031 — Tunes: re-open the note of a glide (D-019); WP-R5 before R3
- **Status:**
  - the order: accepted (human: "adapt the plan so I can call the next agent", 2026-10-12);
  - the rule: **E, the loudness-weighted mean**, accepted 2026-10-13 with WP-R5. MELODY2 had no pre-registered winner: the best, B, beat A by 9.5 points, just under the 10 required; C beat it by 8.6 and E by 8.8. The rule said "keep A and ask", and the human chose E ("1a"). TUNE1 then found E within ±100 c of the ear for 97.6 % of the syllables (the old rule: 47 %). See D-032.
- **Context** (MELODY1, `studies/MELODY1_tapped_melody.md`):
  - R1's tapped Frère Jacques was recognisable. With other random syllables, or with held keys, it was not.
  - With the melody on a plain tone beside it, the human heard that its last notes "go up".
  - Analysis: the D-019 rule sets a syllable without a steady part at the plain mean of the last quarter of its curve. For a falling glide that is the quiet bottom. The ear hears the loud part, 3–6 semitones higher, so the bird shifts the syllable up.
  - Across the bank, 25 % of playable syllables are more than 2 semitones off.
  - The pitch tracker follows the recording; only frames quieter than about −20 dB are unreliable.
  - Held keys add the bird's own melody unless Snap to note is 100 % (which made held-key tunes recognisable).
- **Decision:**
  - WP-R5 comes before R3.
  - It chooses a note rule by a pre-registered blind test (MELODY2: candidates A today, B loud ending, C steady else loudness-weighted mean, D steady parts, E loudness-weighted mean; frames below −20 dB ignored).
  - It rebuilds the bank (same syllables, new note values).
  - The human then chooses how held keys and the release call behave in tunes:
    - (a) phrase only after a hold threshold;
    - (b) no call when the next key follows at once;
    - (c) a "Finch, melody" preset;
    - (d) nothing.
- **Consequences:**
  - D-019's default changes if MELODY2 says so. The four note-rule choices stay available.
  - R3 starts from the rebuilt bank.

### D-032 — Notes by rule E, notes set by ear, and syllables that never start a key press
- **Status:** accepted · 2026-10-13 · WP-R5 (the human: "the corrected pitch should dominate any type of pitch decision"; "1a": keep the 11 notes set by ear)
- **Context:** MELODY2 (rule E chosen, D-031) and TUNE1 (`studies/TUNE1_every_syllable_by_ear.md`). The listener heard every syllable placed on A7 by rule E: 112 of 123 sounded on the note, and 11 were moved by 30–250 c. None was marked "no clear note".
- **Decision:**
  - **Bank builder** (`python -m cuicui.bank`):
    - the default note rule is **E**, the loudness-weighted mean (`BuildSettings.note_rule = "E"`);
    - with E, every anchor (rule, steady, landing, average) ignores the frames more than **20 dB below** the syllable's peak;
    - `--note-rule A` rebuilds the R1 bank exactly (hash `09b16ee69ac8`);
    - new options `--corrections <file>` and `--created-by`.
  - **New file format `cuicui.note_corrections` v1** (`schemas/note_corrections.schema.json`). It lists syllables by annotation segment and label, each with either `note_cents` (the note heard) or `no_clear_note: true`. The factory file is `assets/voicebanks/zf_22938/note_corrections.json` (11 notes, from TUNE1).
  - **A note set by ear replaces every rule's value** for its syllable (rule, steady, landing, average). The syllable gets `rule_used: "ear"`, the flag `"ear"`, and a span covering the whole contour. A correction that names a missing segment or a different label stops the build.
  - **Bank format v1, additive:**
    - `rule_used` may also be "loud ending", "whole" or "ear";
    - the flags "ear", "no_note" (a "no clear note" syllable) and "no_tap" (set by the builder);
    - `build.note_rule`, `build.quiet_floor_db`, `build.corrections`, and the stats `ear_notes` / `no_note`.
  - **Core:** syllables flagged "no_note" or "no_tap" never start a key press.
    - API: `SyllableBank::isTapSyllable`, `tapSyllables()`.
    - `PhraseOrder::begin` redraws a flagged random first pick from the singer's own stream, so every other pick is unchanged. A phrase start moves to its first syllable allowed to start a key press.
    - Such syllables still sing inside phrases.
    - The rule applies only with D-030's reserve (the default playing). The R1 / PROTO1 playing ignores the flags, draw for draw.
  - **Plugin:** the default note-rule choice is labelled **"Loud part"** (host choice text and Play page). The help text says notes tuned by ear win.
  - **The rebuilt factory bank** `zf_22938`, hash `9e0e08577e5a`:
    - unchanged: the same 123 syllables, order, labels, onsets and curves;
    - new: the centres 10545.7 / 10531.2 / 10471.2 / 10424.5 c;
    - no flags apart from "ear" (11) and the existing "hop".
- **Consequences:**
  - Sounds placed by a note change: placement, own-height centre, first syllable on the note.
  - Phrases at Snap to note 0 move as a whole by the centre's change (−43 c).
  - The web prototype carries the new bank.
  - Recordings added in R3 can have their own corrections file.

### D-033 — Held keys in tunes: the "Finch, melody" preset
- **Status:** accepted · 2026-10-13 · WP-R5 (the human, from the held-key comparison page: "generally c is better")
- **Context:** WP-R5 §4.3 offered four options, rendered on three tunes, held 0.35 s or legato:
  - (a) the phrase goes on only after a hold threshold;
  - (b) no closing call when the next key follows at once;
  - (c) a "Finch, melody" preset;
  - (d) nothing.

  MELODY3 (blind) also tested "taps never start with a very wide sweep" (loud part > 8 semitones). It was not adopted: +4.8 points, better on 2 of 4 screens.
- **Decision:**
  - **New factory preset "Finch, melody"** (`assets/presets/05_finch_melody.preset.json`, after the Finch presets): follow the keyboard, Snap to note 100 %, Birds 1. Every syllable of a held key lands on the note, and each new key takes over from the last.
  - **Not picked, so not shipped:** (a) and (b). They were built only as prototype switches for the renders, and removed afterwards.
  - The other factory presets move one number up (06–11); presets are found by name.
- **Consequences:**
  - The default sound and the other presets are unchanged.
  - Tapped tunes (one random syllable per key) are still recognised only on some draws (MELODY3, WP-R5 criterion 3). The human accepted this for now: "i need to move on to improve the performing part". Tap strategies are in the backlog.

### D-034 — Plan: a playing-feel step (WP-R6) after R3
- **Status:** accepted · 2026-10-13 · human
- **Context:** after WP-R5 the human said: "i need to move on to improve the performing part." Then: "it will be important in the plan to add a step to simply give to the user a better feeling of performance. what we aim for is to keep the bird natural but still match the keyboard expressivity. it should organically follow different mode of playing: pizzicato, long notes, give a feeling of control while keeping the organic nature of birds. that's a very tricky challenge that we need to tackle with care, understanding and out of the box fresh ideas."
- **Decision:**
  - New **WP-R6, "Playing feel: the bird follows how you play"** (`work_packages/WP-R6_playing_feel.md`).
  - **Order:** after R3. The human: "definitely not before 'Variations within the bird's style' since it can affect it".
  - **R6 works in three stages**, each ending with the human:
    - understand: scoping questions, the human's own MIDI sketches, the player's and the bird's vocabularies;
    - try fresh ideas as playable prototypes, rendered on those sketches;
    - build the chosen behaviours as a playing mode (D-021).
  - Acceptance: the human's play test, birdness kept (blind, ≥ 90 against the recording), and the first syllable on key-down.
- **Consequences:**
  - The order is now R3 → R6; G1 (release polish) stays after them in the table.
  - R3 should keep its variation controls and syllable families usable from a playing mode (note in WP-R3).


### D-035 — 22938 is a house finch; new recordings, the automatic marker and 48 kHz analysis
- **Status:** accepted · 2026-10-14 · WP-R3 (species: the human, "2b"; accepted with the WP-R3 commit)
- **Context:**
  - D-023 called 22938 a zebra finch. Its sound (tonal sweeps at 2.5–4.9 kHz, no low harmonic stacks, songs ending in a long rising note) fits a house finch. Its Macaulay neighbour 22937 is a house finch. The human: 22938 is a house finch.
  - The human annotation of 22938 stops at 53.3 s; the rest of the recording (134 s, about 313 syllables of the same bird) was one SILENCE segment.
  - The human added `recordings/22937 - House Finch.wav` (cleaned by the human, 108 s, 44.1 kHz).
- **Decision (proposed):**
  - **Species:** 22938 and 22937 are **house finches** (corrects D-023). New banks carry `species: "house finch"`. The factory bank keeps its ID `zf_22938` until the bank choice is decided (R3), so presets and tests keep working.
  - **New tool `cuicui.bank.segment`:** it marks syllables automatically and writes the tseg JSON shape the bank builder reads. It keeps a human annotation's segments unchanged when given one. It was checked against the human's 22938 annotation (all 123 syllables found).
  - **Analysis at 48 kHz:** the bank builder and the marker resample recordings whose rate does not make the tracker's 1 ms hop a whole number of samples (44.1 kHz: 0.998 ms, a drift of 2.3 ms per second) to 48 kHz (`cuicui.bank.build.read_mono`). 48 kHz recordings (22938, the factory bank) are unchanged, bit for bit.
  - Automatic syllables are checked by the human by ear before they enter a bank (INGEST1).
- **Consequences:** `01_research.md` §2.7 (zebra finch priors) does not describe our birds; D-023's design-target remark stands (the recordings set the targets).

### D-036 — Variations within the bird's style: the shape model, two birds merged at load
- **Status:** accepted · 2026-10-14 (with the WP-R3 commit) · WP-R3 (the choices: the human, "1a 2a 3c but keep the data separated. the merge should happen when running"; the details: WP-R3)
- **Amended the same day (feedback round, see the end of this entry):** the plugin's variation is **length and pitch range**, not the shape model.
- **Context:** VARY1 (`studies/VARY1_variations.md`, blind):
  - identical repetition sounded 25 points less like the bird than its own renditions (59.5 vs 84.5);
  - the shape model at the bird's natural spread matched the real renditions (84.0); the nudge reached 73.5;
  - twice the spread broke the bird (46 / 62);
  - new songs (new order) reached 79 against 88.5 for the real songs.
  The pre-registered rule kept nothing (the hidden reference itself got 70 on one screen) and handed the choice to the human.
- **Decision:**
  - **Syllable variation = the shape model** (1a). For one syllable, each value moves from the real rendition's r toward a fresh draw t from its type's spread: r + v·(t − r). The values are the note, the log length, the loudness and 3 shape-mode scores. The real rendition's fine detail is kept. The shape change is centred on the syllable's loud part (rule E), so only the calibrated note change moves the note. Syllables without a type model move their note, length and loudness with the bird's median spread. The nudge is not shipped.
  - **No new order** (2a): the birds sing their real phrases.
  - **Two birds, separate files, merged when the plugin starts** (3c):
    - factory banks `assets/voicebanks/hf_22938/bank.json` (436 syllables, the whole recording) and `hf_22937/bank.json` (358);
    - each with its own `annotation.json`, so one file can be corrected alone, or a bird separated later;
    - `zf_22938` (123 syllables, R1 / R5) stays in the repository for the reference tests and renders; the plugin no longer compiles it.
  - **Bank format v1, additive:**
    - a top-level `variation` section (`cuicui.variation_model` v1: points, the bird's median spread, per type note / log length / loudness mean and sd, mean shape, ≤ 4 modes and their sd);
    - per syllable `type`, `model`, `scores`;
    - schema updated. Built by `python -m cuicui.bank --variation` (`BuildSettings.variation`, off by default, so older banks rebuild unchanged).
  - **New parameter `perf.natural_variation`** ("Natural variation", 0–1, default **1** = the bird's natural spread; Performance; since "R3"). The UI calls it "Variation" (the main knob of the Song card; Pause becomes a small slider there). The C5 `perf.variation` (anatomical generator) is unchanged.
  - **Core API (additive):**
    - `control/variation.hpp`: `makeVariant`, `variantDraws`, `variantSeed`, `resampleLinear` (same arithmetic as `cuicui.variation.variant`);
    - `SyllableBank`: `birds()`, `hasVariation()`, `model()`, `spread()`, `maxLengthMs()`, `find(Query)` (label / length / sweep / bird, for R6), per syllable `bird`, `type`, `model`, `scores`, `peakDb`, `sweepCents`;
    - `mergeBanks(...)`;
    - `SyllableRequest`: `variation`, `variantSeed`, `snapK`, `lengthScale` (R6), `variantNoteCents` (filled by the engine);
    - `PlayContext::variation`;
    - `WhistleVoice::startCurve`; `SyllableEngine::kMaxVariantMs` (per-voice buffers allocated in prepare).
    - The engine removes snapK × the rendition's note change from the placement shift, so at Snap to note 100 % every syllable still lands exactly on the key.
  - **Determinism:** each request's draws come from its own seed (the singer's seed + its request count), never from the order stream. Variation 0 therefore plays the R1 / R5 choices draw for draw. The editor rebuilds the same curve from the seed (`SyllableFeed::Event` gains variation, lengthScale, variantSeed).
  - **Plugin and tools:**
    - plugin: `useBankFiles(paths)` (offline / tests);
    - `plugin_render --bank a.json[,b.json]`; syllable logs gain `variation`, `variant_seed`, `variant_note_cents`, `bird`;
    - Python `cuicui_core.sing(bank_path | [paths], …, {"perf.natural_variation": v})`, `variant(...)`, `variant_draws(seed)`, `bank_summary(paths)`; packages `cuicui.variation` and `cuicui.bank.segment` (D-035).
  - Factory presets: `voice_bank` = "hf_22938+hf_22937" (informative, D-028).
- **Consequences:**
  - The default sound changes: two birds (794 syllables, 59 phrases) and new renditions at the natural spread. Variation 0 = the birds' real syllables. The R5 sound is `--bank assets/voicebanks/zf_22938/bank.json` (zf_22938 has no shape model, so Variation leaves it real).
  - The merged centre (median of both birds, rule E) is 10579.2 c (hf_22938 alone: 10561.2 c, zf_22938: 10545.7 c). At Snap to note 0 (own height) the bird therefore sits 33 c lower than in R5.
  - R6 can drive the amount and the length per request, and find syllables by family, length and sweep.
- **Amendment (2026-10-14, feedback round; VARY2, STRETCH1):**
  - **What happened:**
    - On the listening page the human heard one varied syllable too quiet. The cause was the shape model's loudness variation (a rare draw, a spread inflated by the per-phrase normalisation, a pull toward the type's average). It was removed ("1b"), and every draw is now capped at ±2 sd.
    - VARY2 then found the remaining shape model inaudible ("i cannot tell the difference"): same 73.0, 100 % 71.0, 130 % 75.5, 160 % 75.0, real 80.5.
    - The human asked for length and pitch range only, and chose the spans on the STRETCH1 page: "that's working nicely".
  - **The plugin's Variation** (`perf.natural_variation`, 0–1, default 1):
    - each syllable gets a length scale (80–160 %; labels containing "swipe" up to 200 %) and a pitch-range scale (60–160 %, around its note);
    - draws are log-uniform between lo^v and hi^v (`control::drawStretch`, `StretchSpans`), from the request's own seed;
    - syllables flagged "hop" (tracker jumps) are only narrowed;
    - the loudness and the note are the real ones;
    - ModeAB schedules the next syllable after the stretched one, so the bird's real gaps are kept (`stretchedLengthMs`).
    - Variation 0 = the R1 / R5 playing, draw for draw.
  - **The shape model stays** in the bank files and in `makeVariant` (amount `SyllableRequest::shape`, 0 in the plugin), for later. The nudge and the new order are not used.
  - **API changes (additive):**
    - `makeVariant(…, lengthScale, rangeScale, …)`;
    - `SyllableRequest::rangeScale`, `shape`;
    - `PlayContext::spans`;
    - `BankSyllable::swipe`, `hop`;
    - `SyllableFeed::Event` lengthScale / rangeScale / shape;
    - logs `length_scale`, `range_scale`;
    - Python `cuicui_core.stretch_draw(seed, v, swipe, hop)`, `variant(…, length_scale, range_scale)`, `cuicui.variation.variant(…, range_scale)`.
  - **On zf_22938:** the R5 bank has no shape model, but it varies length and range too; Variation 0 gives the R5 sound.
