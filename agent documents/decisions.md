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

