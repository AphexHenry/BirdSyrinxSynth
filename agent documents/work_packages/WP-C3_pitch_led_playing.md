# WP-C3 — Pitch-led direct playing (MIDI, couplings, ornaments, Snap)

| Field | Value |
|---|---|
| Phase | C — Playability |
| Depends on | C1, C2 |
| Size | L |
| Milestone | **M3 — playable instrument** |

## 1. Goal

Implement the **pitch-led voice controller** so that a musician plays the bird from MIDI **in tune**, with breath, tone and bird-like ornaments, while all physiology is solved underneath: pitch → dependent curves (amplitude, tone, side, OEC, gape) → inverse map → physiological controls. This is the "bird-voiced theremin" (Leash = 0). The generative side (Leash > 0) comes in C5; this WP prepares the interface for it.

## 2. Read first

- `01_research.md` **§7.1–§7.6**, §4.4 (side switching), §4.9
- `02_architecture.md` §6, **§7.2**
- Reports of WP-C1 and WP-C2 (inverse map API, playable ranges), WP-B5 (SideSwitcher)

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/control/voice_controller.hpp` | `VoiceController` |
| `core/include/cuicui/control/gesture_source.hpp` | `GestureSource` interface + `NullGestureSource` |
| `core/include/cuicui/control/ornaments.hpp` | scoop, FM/trill, fall |
| `core/include/cuicui/control/snap.hpp`, `scales.hpp` | Snap and scales |
| `plugin/Source/PluginProcessor.*` | uses `VoiceController`; MIDI mapping; perform/physiology mode |
| `params.def` | add Layer-3 parameters with "Since C3" in `02_architecture.md` §7.2 |
| `core/tests/test_voice_controller.cpp`, `plugin/tests/test_perform_mode.cpp` | tests |
| `examples/midi/*.mid` | test phrases (scale, melody, legato phrase, jumps) created by script |

## 4. Specification

### 4.1 Interfaces

```cpp
namespace cuicui::control {
struct GestureSample { double pitchCents; double ampDb; double tone; double side; bool active; /* hints */ double oecRatio; double gape; double secondVoiceCents; };
class GestureSource {                         // implemented by C4/C5/F3
public:
    virtual ~GestureSource() = default;
    virtual void noteOn(double anchorCents, int velocity, uint32_t seed) noexcept = 0;
    virtual void noteOff() noexcept = 0;
    virtual GestureSample next(double dtSeconds) noexcept = 0;   // called at control rate
};

struct PerformInputs {       // from MIDI + host parameters, per control tick
    bool gate; double noteCents; double bendCents; double velocity01; double pressure01 /* aftertouch/MPE */;
    double breath, tone, physics, motion, range, speed, snap, glideMs, size;
    int scale, root; double oecTracking, gapeTracking, sideSwitchCents; bool rangeFold;
    double leash; /* used from C5, 0 here */
};

class VoiceController {
public:
    void prepare(double controlRate, const InverseMap& map) noexcept;
    void setGestureSource(GestureSource* src) noexcept;          // may be null
    void tick(const PerformInputs& in, const VoiceSettings& base, ControlFrame& out) noexcept;
    struct Debug { double targetCents, f0Target, nu, alpha, beta, tone, side; bool folded; } debug() const noexcept;
};
}
```

Control rate: host rate / 32 (≈ 1.5 kHz) — the processor calls `tick` at each sub-block boundary; `BirdVoice` ramps between frames.

### 4.2 Pitch path (per tick)

1. **Glide:** $P_{note}$ follows the latest note with a one-pole in cents, time constant `perf.glide_ms` (legato only; a new note after silence jumps immediately).
2. **Direct pitch:** $D = P_{note} + \text{bend} + O(t)$ with ornaments O (§4.4).
3. **Leash blend (prepared):** $P = (1-\lambda)D + \lambda G$, λ = `perf.leash` (0 in this WP; G from the gesture source if present, else G = D).
4. **Snap** (research §7.5): $P \leftarrow P + \text{snap}\cdot w\,(Q(P) - P)$, $w = e^{-|\dot P|/v_0}$, $v_0$ = 2000 cents/s, Q = nearest allowed pitch of `perf.scale`/`perf.root` (chromatic = nearest semitone).
5. $f_0 = 440\cdot 2^{(P - 6900)/1200}$; γ = γ₀/σ; ν = 2π f₀/γ.

### 4.3 Dependent curves

- **Amplitude/breath:** $u$ (pressure command) = gate · clamp(breath · (0.6 + 0.4·velocity) · (1 + aftertouch·0.5) · $10^{E_{gest}/20}$). Note-off → u = 0 (physical release).
- **Tone:** `perf.tone` + MPE Y (CC74) offset + gesture tone (when λ > 0: blend).
- **Roughness coupling:** $\sigma_\beta = \sigma_{\beta,base} + 0.03\cdot\max(0,\ 0.15 - \text{tone})/0.15$ (buzz near threshold).
- **OEC:** $f_H = f_{H,base}\,(f_0/f_{ref})^{\kappa}$, $f_{ref}$ = 1000 Hz/σ, κ = `perf.oec_tracking`; if a gesture hint `oecRatio` is set: $f_H = \text{oecRatio}\cdot f_0$ (clamped to range). $f_{H,base}$ = host `oec.freq_hz`.
- **Gape:** $g = g_{base} + \kappa_g\log_2(f_0/f_{ref})$, clamped; $g_{base}$ = host `beak.gape`; hint overrides.
- **Solve:** `InverseMap::solve(nu, tone, alphaBreath, physics, rangeFold)` with $\alpha_{breath} = -0.10 + 0.55\,u$ (so physics = 1 reproduces the physiological path).
- **Pressure vs α (important):** B-phase physiology stays untouched: inside the voice $\alpha = \alpha_{base}(p) + \alpha^{off}$ with the *dynamic* pressure p and $\alpha_{base}(p) = -0.10 + 0.55\,p$. The controller therefore:
  1. separates the **player breath** $u_{pl}$ (breath × velocity × aftertouch) from the **command** $u = u_{pl}\cdot 10^{E_{gest}/20}$ sent as `resp.pressure`;
  2. computes the timbre target $\alpha_T$ from `InverseMap::solve` with $\alpha_{breath} = \alpha_{base}(u_{pl})$;
  3. sets $\alpha^{off} = \alpha_T - \alpha_{base}(u_{pl})$ — updated while the gate is on, **frozen during the release** (so α falls below threshold as p decays and the note stops);
  4. runs a **mirror of the respiratory drive** (same τ values, exact one-pole at control rate) to predict $\hat p$, computes $\hat\alpha = \alpha_{base}(\hat p) + \alpha^{off}$ and looks up $\beta = B(\hat\alpha, \nu)$ (with $\hat\alpha$ clamped into $[\alpha_{lo}, \alpha_{hi}]$ for the lookup) — so pitch stays in tune during swells, gesture amplitude dips and attacks.
  Consequences: gesture gaps (amplitude → −∞ dB) and note-offs produce physical offsets; quieter moments sit closer to threshold (richer), which is bird-like. Document this clearly in code.
- **Sides:** active side chosen by rule: start on L; switch when |ΔP| > `perf.side_switch_cents` within 30 ms, or when P crosses the split pitch (median of playable range) ± 100 cents hysteresis (higher → R). `SideSwitcher` crossfade 4 ms. The inactive side's β is pre-tuned by solving the same ν with its own α. If `secondVoiceCents` hint is set: both gates open, second side solves P + interval.

### 4.4 Ornaments (Motion, Range, Speed)

All seeded (per note) randomisation ±10 % of rates/depths.
- **Scoop-in** at note start (not on legato): $O_s(t) = -A_s e^{-t/\tau_s}$, $A_s$ = 300·Range·Motion cents, τ_s = (15 + 25·(1−Motion)) ms / Speed.
- **Trill/vibrato FM** after onset delay 80 ms/Speed with 50 ms fade-in: rate = (15 + 20·Motion)·Speed Hz, depth = 150·Motion²·Range cents; waveform morphs from sine (Motion < 0.5) to a smoothed square (trill-like steps, 1 ms smoothing) as Motion → 1.
- **Fall** at note-off during release: $O_f$ = −200·Range·Motion cents over the release time.

### 4.5 MIDI mapping and modes

- Mono, last-note priority, legato. Velocity, channel/poly aftertouch, MPE pressure → breath modifiers; pitch bend (±`perf.bend_range_st`), MPE X; CC74 → tone offset; CC1 → Motion (added to the host value, clamped); CC64 sustain → hold gate.
- `perf.mode` = physiology: C1 behaviour (direct parameters, crude tuning). `perf.mode` = perform: controller drives β, α offsets, gates, OEC, gape; the host values of those parameters act as described in §4.3 (bases) or are ignored (β, α offsets, gates). Document the "driven" semantics in the report and in parameter tooltips later (C6).

## 5. Suggested steps

1. `VoiceController` with pitch path, solve, dependent curves; unit tests with a fake inverse map and with the real one.
2. Processor integration and MIDI mapping; perform/physiology switch.
3. Sides and SideSwitcher decisions.
4. Ornaments and Snap.
5. MIDI test files, tuning measurements, listening renders.

## 6. Out of scope

Gesture generation (C4/C5), UI (C6), polyphony (C5), tempo sync (C5).

## 7. Tests & acceptance criteria

Measured with `plugin_render` renders + D2 feature extractor (f₀ tracks) unless stated.

| # | Criterion |
|---|---|
| 1 | Tuning (Motion 0, Snap 0, tone 0.6, σ = 1): each note of the chromatic test file within ±5 cents (median of the steady part). |
| 2 | Tuning at tone 0.2 and 1.0, σ = 0.5 and 2 (every 3rd semitone): ±5 cents (tone 0.2 allowed ±10). |
| 3 | Glide: 60 → 72 legato with glide 100 ms: pitch reaches 63.2 % of the interval at 100 ms ± 10 %. |
| 4 | Bend ±2 st → ±200 cents ± 5. |
| 5 | Snap: a fake gesture source adds a steady +37 cents offset: with snap = 1 the steady pitch error ≤ 3 cents; during a 1200-cent/100 ms sweep the pitch path differs from the unsnapped path by < 10 % of the sweep. |
| 6 | OEC tracking: telemetry f_H follows $(f_0/f_{ref})^\kappa$ within 1 % for κ ∈ {0, 0.7, 1}. |
| 7 | Side switch: octave jump (legato, glide 0) triggers a side change; no f₀ estimates (2 ms windows) in the middle 60 % of the interval for more than 2 consecutive windows. |
| 8 | Onset latency (note-on → output envelope −20 dB of steady state) ≤ 20 ms at defaults; report per tone value. |
| 9 | Physics = 1 + breath modulation: pitch stays within ±5 cents. |
| 10 | Determinism with fixed seed; zero allocations in `processBlock`; pluginval 5 passes; `scripts/check.sh` passes. |

## 8. Listening & results — milestone M3

`plugin_render` into `listening/C3/`:

| File | Content | Listen for |
|---|---|---|
| `01_scale_motion0.wav` | C major scale over the playable range | clean, in tune, bird timbre, physical onsets |
| `02_melody_motion0.wav`, `03_melody_motion05.wav`, `04_melody_motion1.wav` | same melody, Motion 0 / 0.5 / 1 | from theremin-like to scooping, trilling bird |
| `05_tone_cc74_sweep.wav` | sustained note, CC74 sweeping tone | timbre morph at fixed pitch |
| `06_breath_aftertouch.wav` | note with aftertouch swells, physics 0.5 | breathing dynamics and colour |
| `07_jumps_side_switch.wav` | legato octave and tenth jumps | crisp two-sided jumps |
| `08_snap_demo.wav` | Motion 1 melody with snap 0 then 1 | ornaments keep moving but notes land in tune |
| `09_sizes.wav` | melody at σ = 0.5, 1, 2 | tiny bird → big bird, still in tune |
| `10_legato_glide.wav` | legato phrase with glide 60 ms | smooth portamento |

Figures: f₀ tracks vs target for 01 and 04; tuning error table.

Final message: **milestone M3**. Human checklist: play the Standalone/DAW plugin from a keyboard with mod wheel, aftertouch (if available) and pitch bend; comment on feel of Motion defaults, onset responsiveness, and tuning. Expect a feedback round.

## 9. Pitfalls

- Never feed the inverse map a ν computed with the wrong γ (size!).
- Ornament FM must be applied in cents before `solve`, never by modulating β directly (keeps tuning and tract couplings consistent).
- Legato after note-off during release: decide and document (default: treat as new note if pressure fell below 20 %).

## Notes from WP-C2

- Runtime: `cuicui::control::InverseMap` in `core/include/cuicui/control/inverse_map.hpp`. Tables are compiled into `core/include/cuicui/generated/atlas_tables.hpp` (no file I/O). Python: `cuicui_core.inverse_solve(nu, tone, alpha_breath, physics, range_fold)` and `inverse_nu_range(tone)`.
- Always pass **current** γ: `ν = 2π f₀ / γ` with `γ = 24000 / body.size`. At σ = 1 the playable band is about **D6–C8** (1162–4202 Hz). Notes below that sit on the SNIC/onset floor and were dropped from the map (several–hundred cents of interpolation error).
- `perf.range_fold` is already in `params.def` (default on). The plugin does not call `InverseMap` yet — C3 must replace the C1 empirical `f₀(β)` table.
- Closed-loop tests measure f₀ with an interpolated FFT peak near the target. Zero-crossings fail on rich full-voice pulses; integer-lag autocorrelation is ~20 cents coarse near 6 kHz.

## Notes from WP-C1

C1 MIDI no longer uses Hopf $\beta=(2\pi f/\gamma)^2$ (that sat near 0.02 on piano Hz and made C≈D). It inverts an empirical labial $f_0(\beta)$ table at playing $\alpha$ (vel 100) so C4–C5 is about $2\times$ acoustically, still at bird frequencies (C4 $\approx$ 2.47 kHz, not 261 Hz). Velocity still shifts pitch via $\alpha$. C3 must replace this with the atlas inverse map (and size/γ); do not keep the C1 table.

