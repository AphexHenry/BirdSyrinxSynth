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
