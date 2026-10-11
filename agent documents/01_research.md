# Part 1 — Research & Concept: an anatomical songbird synthesizer for musicians

> Status: reference document. Agents READ this; they do not edit it without a logged decision (see `decisions.md`).
> Scope: **songbird (oscine) syrinx only**. Target species for data-driven work: **zebra finch** (*Taeniopygia guttata*).
> Audience of the final product: **musicians**. Science is the engine; playability is the priority.
> **Update 2026-10-11:** listening studies changed several assumptions of this document. The bird's identity is in its real gestures; the default voice is a whistle with the bird's own harmonics, not the Tier-1 engine (D-022); this bird's repertoire has no low stacks (D-023). See `reviews/2026-10-11_approach_review.md` and `decisions.md` D-018…D-024.

---

## 0. Reading guide

### 0.1 Evidence tags

Every modelling choice in this document is tagged so agents know what is solid and what is ours to tune:

| Tag | Meaning |
|---|---|
| **[L]** | From the scientific literature (reference given). Change only with a good reason. |
| **[V]** | Verified numerically while preparing this document (quick Python simulation). Agents must re-verify in tests. |
| **[D]** | Design decision of this project (musical or engineering choice). Free to tune, but log significant changes in `decisions.md`. |

### 0.2 Sign conventions

Published papers use different sign conventions for the same normal form (e.g. $+\alpha\gamma^2$ vs $-\alpha\gamma^2$). **This project uses exactly the equations written in §4.3**, where oscillation (sound) happens for **positive α** (more pressure) and frequency **increases with β** (more tension). When reading papers, convert before comparing numbers.

### 0.3 Where things are used

| Section | Used by work packages |
|---|---|
| §2 Anatomy | background for all |
| §4 Tier-1 equations | B1–B6 |
| §5 Tier-2 source | G2 (optional) |
| §6 Numerics / real time | A2, B1–B6, C1 |
| §7 Playability | C2–C6, F1–F4 |
| §8 Fitting | D1–D2, E1–E4 |
| §9 Evaluation | all WPs with listening/metrics |

---

## 1. Goals and design principles

1. **Musicians first.** The instrument must be playable from a MIDI keyboard/controller in real time, sound like a bird even when played badly, and stay in tune when asked to.
2. **Anatomy as the engine, not the interface.** Physiological parameters (air-sac pressure, labial tension…) are the internal "truth", but musicians play pitch, breath and character. Experts can reach the physiology.
3. **Minimal but fittable.** Every parameter must earn its place: either it is audible and controllable, or it is identifiable from recordings. The default engine ("Tier 1") is a low-dimensional model already shown to reproduce zebra finch song in the literature. A more detailed engine ("Tier 2") is optional.
4. **Pitch leads.** Birds are defined by fast variations, pitch being the dominant one. Pitch is the master curve; other controls are derived from it (§7.3).
5. **Science–art sweet spot.** Real zebra finch data provides *likely* gestures; the performance engine generates *variations* whose distance from reality is a musical control ("Variation", §7.8).
6. **Real time and robust.** One voice must cost well under 1% of a modern CPU core; no allocations or locks on the audio thread; stable for all parameter values reachable from the UI.
7. **Offline analysis can be slow.** Fitting real recordings may take hours; the *result* must play back in real time and remain editable.

---

## 2. The songbird vocal organ — anatomy and function

### 2.1 The production chain

```
 respiratory muscles ──► air sacs ──► bronchi (left, right)
                                         │        │
                                     ┌───▼────────▼───┐
                                     │     SYRINX     │  two independent sound sources
                                     │  L labia  R labia  (medial + lateral labia per side)
                                     └───────┬────────┘
                                             │  sound = modulated airflow
                                         TRACHEA (tube, ~33 mm in zebra finch)
                                             │
                                  LARYNX / GLOTTIS (constriction)
                                             │
                        OROPHARYNGEAL-ESOPHAGEAL CAVITY (OEC, variable volume)
                                             │
                                          BEAK (variable gape)
                                             │
                                        RADIATION to air
```

Unlike mammals, the bird's sound source (syrinx) sits at the **bottom** of the trachea, where the bronchi join. The larynx at the top does not produce voice.

### 2.2 Respiration and air sacs

- Song is produced on expiration. Expiratory muscles pressurise the air sacs; the **subsyringeal (air-sac) pressure** drives airflow through the syrinx. **[L]** (Suthers & Zollinger 2004; Mindlin & Laje 2005)
- Pressure must exceed a **phonation threshold** before the labia oscillate; the onset is a dynamical bifurcation, not a linear fade-in. This is why bird onsets have a characteristic "snap". **[L]** (Mindlin & Laje 2005; Elemans et al. 2015)
- Pressure mainly controls **loudness and the onset/offset of sound**, with a secondary effect on frequency. **[L]** (Goller & Suthers 1996)
- Between syllables birds take short inspirations ("mini-breaths"), which create natural silences and phrasing. **[L]** (Suthers & Zollinger 2004)

### 2.3 The syrinx

- **Structure.** In songbirds the syrinx is *tracheobronchial*: each bronchus has its own pair of vibrating tissue folds, the **medial and lateral labia**. Two sides = **two independent sound sources**. **[L]** (Suthers 1990; Goller & Larsen 1997)
- **Mechanism.** Airflow sets the labia into self-sustained oscillation (myoelastic–aerodynamic mechanism, the same class of physics as human vocal folds). Each oscillation cycle opens and closes the airway, producing a train of air pulses = the sound source. **[L]** (Elemans et al. 2015)
- **Frequency control.** Fundamental frequency is controlled mainly by **tension** of the labia, set by syringeal muscles; the ventral syringeal muscle (vS) activity correlates with frequency. **[L]** (Goller & Suthers 1996)
- **Gating.** Other syringeal muscles (dorsal/ventral tracheobronchial) adduct/abduct the labia and can **close one side**, silencing it. **[L]** (Goller & Suthers 1996)
- **Two voices and side switching.** Songbirds can sing with one side, both sides, or switch sides within a note. In several species studied (e.g. northern cardinal) the left side tends to produce lower and the right side higher frequencies; broad frequency sweeps can be split across the two sides. Simultaneous independent sides produce "two-voice" notes with non-harmonically related components. **[L]** (Suthers 1990; Suthers & Goller 1997)
- **Nonlinear phenomena.** Because the source is a nonlinear oscillator, bird song contains *period doubling, subharmonics, frequency jumps and deterministic chaos*; these are part of what makes sounds rough or "buzzy". **[L]** (Fee et al. 1998; Mindlin & Laje 2005)

### 2.4 The trachea

- A flexible tube from syrinx to larynx; **≈33 mm long in the zebra finch**. It acts as an acoustic waveguide with resonances. **[L]** (Daley & Goller 2004)
- Tracheal length changes during zebra finch song are tiny (~3 %, a few tenths of a mm) and acoustically insignificant; the trachea is therefore modelled with **constant length per bird**, only scaled by the "Size" control. **[L]** (Daley & Goller 2004)
- Simple tube resonances: closed–open (quarter wave) $f_k = (2k-1)\,c/(4L)$; closed–closed or open–open (half wave) $f_k = k\,c/(2L)$. With $L = 33$ mm and $c \approx 350$ m/s: quarter-wave 2.65 kHz, 7.95 kHz…; half-wave 5.3 kHz, 10.6 kHz… Which boundary condition dominates depends on the glottis/OEC loading and is **treated as a fitted parameter** (reflection coefficient sign, §4.6). **[D]**

### 2.5 The upper vocal tract: glottis, OEC, beak

- **Glottis/larynx:** a narrow passage between trachea and the oropharyngeal cavity. Acoustically: an *inertance* (mass of air in a narrow tube). **[L]** (Fletcher et al. 2006)
- **OEC (oropharyngeal–esophageal cavity):** songbirds actively expand the upper esophagus and oropharynx while singing. It acts as a **Helmholtz resonator** whose resonance is *tuned to track the fundamental frequency* of the song (larger volume for lower notes). **[L]** (Riede et al. 2006; Fletcher et al. 2006)
- **Zebra finch:** the upper vocal tract (OEC and beak) is actively articulated during song. **[L]** (Ohms et al. 2010; Goller et al. 2004)
- **Beak gape:** the beak acts as an opening (inertance + radiation). Beak gape tends to **increase with frequency** in songbirds, which shifts the tract resonance upwards. **[L]** (Westneat et al. 1993; Hoese et al. 2000)
- **Tongue:** relevant in parrots, not in our scope. Not modelled.

### 2.6 Radiation

Sound radiated from the beak opening behaves at low frequencies like a small monopole source: the far-field pressure is proportional to the **time derivative of the volume flow** out of the beak. This gives a +6 dB/octave tilt. **[L]** (standard acoustics; Fletcher 1992)

### 2.7 Zebra finch song: what the synthesizer must reproduce

- **Structure.** A song bout starts with several **introductory notes**, followed by one or more repetitions of a stereotyped **motif** of typically 3–8 **syllables**. Each adult male sings essentially **one motif**, very consistently. **[L]** (Zann 1996; Tchernichovski et al. 2000)
- **Syllables** last roughly 50–200 ms and can contain several acoustically distinct **notes**. Silences separate syllables.
- **Sound types** typical of zebra finch song **[L]** (Zann 1996; Tchernichovski et al. 2000; Daley & Goller 2004):
  - **Harmonic stacks:** low fundamental (~500–600 Hz, and up to ~1 kHz), many harmonics; **upper harmonics are often louder than the fundamental** → strong vocal-tract filtering and a spectrally rich source.
  - **High notes / whistles:** fundamental ≥ 1.5 kHz, few harmonics.
  - **Noisy / broadband notes** ("buzzes"): noise-like spectra, rough, possibly chaotic source dynamics.
  - **Frequency-modulated notes:** fast up/down sweeps.
  - **Introductory notes / calls:** short, harmonic, often with a downward sweep.
- **Musical palette warning.** Zebra finch song is buzzy, dense and stereotyped. It is an excellent scientific model and a *narrow* musical palette. The archetype library (§7.9) compensates; the fitting pipeline is species-agnostic within songbirds and can later be applied to other songbird recordings (see `backlog.md`).

---

## 3. From anatomy to DSP: mapping table

| Anatomy | Physical role | Model | DSP module (C++) | Main parameters | Tag |
|---|---|---|---|---|---|
| Expiratory muscles + air sacs | pressure source | 1st-order pressure dynamics | `RespiratoryDrive` | `resp.pressure`, `resp.attack_ms`, `resp.release_ms` | [D] structure, [L] role |
| Syrinx labia (one side) | self-oscillating valve | normal form of Takens–Bogdanov bifurcation (Tier 1) | `LabialOscillator` | α (from pressure), `src.X.beta`, γ | [L] |
| Syringeal muscles (vS) | tension | β control | inside `LabialOscillator` | `src.L.beta`, `src.R.beta` | [L] |
| Syringeal muscles (dTB/vTB) | gating one side | α offset + source gain per side | `BilateralSource` | `src.L.gate`, `src.R.gate` | [L] role, [D] implementation |
| Turbulence, muscle jitter | noise | noise on β + aspiration noise | `NoiseSources` | `noise.beta_sigma`, `noise.aspiration` | [L]+[D] |
| Trachea | waveguide | delay with reflection (comb), fractional delay | `Trachea` | `trachea.length_mm`, `trachea.reflection`, `trachea.loss` | [L] |
| Glottis + OEC + beak | Helmholtz resonator + openings | 3rd-order acoustic circuit | `UpperVocalTract` | `oec.freq_hz`, `oec.damping`, `beak.gape`, `beak.damping` | [L] |
| Beak opening → air | radiation | derivative of beak flow | `Radiation` (inside `UpperVocalTract` + output stage) | `out.radiation`, `out.gain_db` | [L] |
| Body size | scales everything | coherent scaling law | `BodyScaling` (in `BirdVoice`) | `body.size` | [D] |
| Brain / motor program | gestures | pitch-led controller, gesture generator | `VoiceController`, `GestureEngine` | `perf.*` | [D], inspired by [L] motor gestures |

---

## 4. Tier-1 model: equations

### 4.1 Signal flow (one voice)

```
MIDI / macros / gestures
        │
  VoiceController (pitch-led, §7)  ── produces per-sample physiological controls ──┐
                                                                                   ▼
 resp.pressure ─► RespiratoryDrive ─► p(t) ─► α_base(t), G(t)
                                              │
                         ┌────────────────────┴───────────────────┐
                 LabialOscillator L (α_L, β_L)          LabialOscillator R (α_R, β_R)
                         │ x_L(t)                                  │ x_R(t)
                         └──────► BilateralSource: s(t) = Σ gains·(x − DC) + aspiration noise
                                              │
                                        Trachea (comb waveguide)
                                              │ p_t(t)
                                   UpperVocalTract (glottis–OEC–beak circuit)
                                              │ U_b(t), dU_b/dt
                                   Radiation / output mix, DC block, gain
                                              │  (internal rate f_int)
                                   Downsampler (anti-alias) ─► host sample rate
```

Everything from `RespiratoryDrive` to `Radiation` runs at the **internal oversampled rate** $f_{int}$ (§6.1). Controls are updated per host sample (or per sub-block ≤ 32 samples) and linearly interpolated.

### 4.2 Respiratory drive **[D]**

Command $u(t) \in [0,1]$ (`resp.pressure`, driven by Breath/velocity in performance mode). Normalised air-sac pressure $p(t)$:

$$
\frac{dp}{dt} = \frac{u(t) - p(t)}{\tau(t)}, \qquad
\tau(t) = \begin{cases} \tau_{att} & u > p \\ \tau_{rel} & u \le p \end{cases}
$$

Defaults: $\tau_{att} = 8$ ms, $\tau_{rel} = 15$ ms (`resp.attack_ms`, `resp.release_ms`). Discretise exactly: $p[n+1] = u + (p[n]-u)\,e^{-h/\tau}$.

Mapping to the oscillator's bifurcation parameter (before per-side offsets, see §4.4):

$$
\alpha_{base}(t) = \alpha_{rest} + p(t)\,(\alpha_{max} - \alpha_{rest}), \qquad \alpha_{rest} = -0.10,\ \alpha_{max} = 0.45
$$

Source gain from flow (Bernoulli: flow ∝ √pressure) **[D, physically motivated]**:

$$ G(t) = \sqrt{\max(p(t), 0)} $$

In performance mode α is partly decoupled from pressure (the "Physics" control, §7.4); this section describes the purely physiological path.

### 4.3 Labial oscillator — normal form **[L]**

The labial dynamics of one side are reduced to the normal form of a **Takens–Bogdanov bifurcation with cubic terms**, introduced and validated for birdsong by the Mindlin group (Gardner et al. 2001; Mindlin & Laje 2005; Amador et al. 2013; Boari et al. 2015):

$$
\boxed{
\begin{aligned}
\dot x &= y \\
\dot y &= -\alpha\gamma^2 \;-\; \beta\gamma^2 x \;-\; \gamma^2 x^3 \;-\; \gamma x^2 y \;+\; \gamma^2 x^2 \;-\; \gamma x y
\end{aligned}}
$$

- $x$: labial displacement (dimensionless), $y$: its velocity.
- $\alpha$: unfolding parameter associated with **air-sac pressure** (sound for α > 0 in this convention).
- $\beta$: unfolding parameter associated with **labial tension** (frequency increases with β).
- $\gamma$: time-scale factor. Published values: 23 500–24 000 s⁻¹. Default **γ₀ = 24 000**. All frequencies of the oscillator scale **linearly with γ** (time rescaling).

**Sound source.** The airflow modulation is proportional to the labial displacement: the source signal is $x(t)$ with its DC removed (published implementations inject $A\,x(t)$ into the trachea, Sitt et al. 2010; Arneodo et al. 2021).

#### 4.3.1 Fixed points and bifurcations (analytic, this convention) **[V]**

Fixed points ($y = 0$) satisfy

$$ \alpha = \varphi(x) := x^2 - x^3 - \beta x .$$

Jacobian at a fixed point: $\mathrm{tr} J = -\gamma\,x(1+x)$, $\det J = \gamma^2(\beta + 3x^2 - 2x) = -\gamma^2\varphi'(x)$.

- **Hopf line:** $\alpha = 0$ with $\beta > 0$ (fixed point at $x=0$). Near it the oscillation is almost sinusoidal with angular frequency $\omega \approx \gamma\sqrt{\beta}$, i.e.

$$ f_0 \approx \frac{\gamma\sqrt{\beta}}{2\pi} \qquad\text{(e.g. } \gamma = 24000,\ \beta = 0.5 \Rightarrow 2701\text{ Hz, confirmed numerically)} $$

- **Saddle-node curve** (parametrised by $x$):

$$ (\alpha, \beta) = \big(2x^3 - x^2,\; 2x - 3x^2\big), \qquad \text{exists for } \beta \le 1/3,\ \text{cusp at } (\alpha,\beta) = (-1/27,\ 1/3). $$

  Its upper branch ($x > 1/3$) is a **saddle-node on invariant circle (SNIC)** for the parameter region of interest: oscillations are born with *finite amplitude and near-zero frequency*, i.e. relaxation-like pulses with **rich spectra**. Examples ($\gamma = 24000$), analytic onset vs simulation: β = −0.10 → α_on = 0.217 (sim 0.22); β = −0.05 → 0.182 (sim ≈ 0.19); β = 0.05 → 0.116 (sim ≈ 0.12); β = 0.15 → 0.054 (sim ≈ 0.05).
- **Bistability / homoclinic region:** for roughly 0 < β < 0.25 there is a region between the Hopf line and the saddle-node curve where oscillation depends on the initial state (hysteresis). **The effective onset boundary must be computed numerically** by ramping α up from the silent rest state (and down again for offset). This hysteresis is musically useful (natural legato) and must be documented in the atlas (WP-C2).
- **Onset when starting from rest [V].** At α_rest = −0.1 the rest state lies on the upper branch of fixed points ($x \approx 1$). Ramping α up slowly (0.2 s⁻¹), oscillation starts at

$$
\alpha_{on}(\beta) \approx \begin{cases} 2x_u^3 - x_u^2, \quad x_u = \dfrac{1 + \sqrt{1 - 3\beta}}{3} & \beta < 0.25 \ \text{(SNIC onset, rich)}\\[2mm] 0 & \beta \ge 0.25 \ \text{(Hopf onset, amplitude grows like } \sqrt{\alpha}\text{)}\end{cases}
$$

  Ramp simulation vs formula: β = −0.10: 0.218 vs 0.217; β = 0.05: 0.118 vs 0.116; β = 0.15: 0.056 vs 0.054; β = 0.20: 0.028 vs 0.026; β = 0.24: 0.006 vs 0.005. For β ≥ 0.25 the tone is born at α = 0 but reaches audible amplitude a little later (β = 0.5: amplitude 0.05 at α ≈ 0.026 with this ramp speed).

#### 4.3.2 Regimes (what you hear) **[V]**

Simulated with γ = 24 000 (source waveform $x$ only, no tract):

| Region | Where | f₀ range | Spectral content index SCI = centroid/f₀ | Character |
|---|---|---|---|---|
| Silent | α below onset boundary | – | – | rest |
| Rich / pulsed | just above SNIC curve, β ≲ 0.15 | from ~150 Hz up to ~1.5 kHz | 1.3 – >2 (very high right at onset) | buzzy, reedy, zebra-finch-stack-like after tract filtering |
| Tonal | β ≳ 0.2, α well above boundary | ~1.5 – 3.9 kHz (β ≤ 0.8) | 1.03 – 1.1 | whistle-like |

Frequency depends on **both** α and β (e.g. β = 0.35: α = 0 → 2.26 kHz, α = 0.4 → 3.80 kHz). At fixed α (in the oscillating region), **f₀ increases monotonically with β** — this is what makes the inverse "pitch → β" map possible (§7.4). Higher pitch ranges are reached with larger β or larger γ (smaller body size).

#### 4.3.3 Numerics **[V]**

- Integrate with **classical 4th-order Runge–Kutta (RK4)** at the internal rate $f_{int}$.
- Measured pitch error vs a 128× reference (points in tonal and rich regions): **native 44.1 kHz: 10–13 cents** (not acceptable); **2× (88.2 kHz): < 1 cent**; **4× (176.4 kHz): converged**. → Policy: $f_{int} \ge 176.4$ kHz (§6.1).
- Use `double` for oscillator state. Parameters are held constant within one RK4 step (sample-and-hold at internal rate after interpolation).
- Clamp state for safety: if $|x| > 10$ or non-finite, reset to rest state and flag (should never happen for parameters within registry ranges; test it).
- A published real-time implementation used forward Euler with many sub-steps (18 per sample at 48 kHz, Sitt et al. 2010) — RK4 at 4× is cheaper for the same accuracy.

### 4.4 Bilateral source and gating **[L role, D implementation]**

Two independent oscillators, left (L) and right (R), share the air-sac pressure but have their own tension and gating:

$$
\alpha_s(t) = \alpha_{base}(t) + \alpha^{off}_s - \big(1 - g_s(t)\big)\,\Delta\alpha_{close}, \qquad s \in \{L, R\}
$$

- $g_s \in [0,1]$: gate (`src.L.gate`, `src.R.gate`); 1 = side open. $\Delta\alpha_{close} = 0.6$ guarantees silence when closed.
- $\alpha^{off}_s$: per-side offset (`src.L.alpha_offset`, `src.R.alpha_offset`), default 0.
- Source signal (DC removed by a one-pole DC blocker at 20 Hz per side):

$$ s(t) = G(t)\,\big[\,g_L\,\tilde x_L(t) + g_R\,\tilde x_R(t)\,\big] + n_{asp}(t) $$

- Optional weak coupling **[D]** (off by default, `src.coupling` = ε): add $\varepsilon\,\gamma^2\,(x_{other} - x_s)$ to $\dot y_s$. Gives locking and "two-voice beating" effects.
- **Side switching** (performance layer, WP-B5/C3): a large pitch jump is realised by pre-tuning the *closed* side to the new pitch, then crossfading gates over 2–6 ms. The inactive side's oscillator keeps running below threshold, so re-opening produces a physical onset.

### 4.5 Noise sources **[L + D]**

1. **Tension noise [L]** (noise added to β in published synthetic-song experiments, Frontiers in Neuroscience 2021, doi:10.3389/fnins.2021.647978):
   $$ \beta_s^{eff}(t) = \beta_s(t) + \sigma_\beta\, \eta_s(t) $$
   $\eta_s$: unit-variance white noise low-passed (one-pole) at $f_\eta$ = 300 Hz (`noise.beta_sigma` = σ_β, default 0.005; `noise.beta_bandwidth_hz`). Near the onset boundary this produces natural irregularity and roughness; far from it, subtle jitter.
2. **Aspiration / turbulence noise [D]**, proportional to flow and pulsed by the labial opening (analogous to glottal noise in voice synthesis):
   $$ n_{asp}(t) = a_{asp}\, G(t)\, \Big(\tfrac{1}{2} + \tfrac{1}{2}\tanh\big(k\,\bar x(t)\big)\Big)\, \mathrm{BP}\{w(t)\} $$
   $w$: white noise; BP: 2nd-order band-pass centred 4 kHz, Q = 0.7; $\bar x$ = mean of open sides' $\tilde x$; $k = 4$. `noise.aspiration` = $a_{asp}$, default 0.02.

### 4.6 Trachea **[L]**

Published form (Sitt et al. 2010; Arneodo et al. 2021), with $\tau = 2L/c$ the round-trip delay:

$$
p_i(t) = A\,s(t) - r\,p_i(t - \tau), \qquad
p_t(t) = (1 - r)\,p_i(t - \tau/2)
$$

- $p_i$: pressure at the tracheal input (syrinx end), $p_t$: pressure transmitted to the glottis.
- Some papers write the output delay as τ instead of τ/2; the difference is a pure delay < 0.2 ms. We use τ/2 (one-way travel time).
- Transfer function: $H_{tr}(\omega) = \dfrac{A(1-r)\,e^{-j\omega\tau/2}}{1 + r\,e^{-j\omega\tau}}$.
  - $r > 0$: peaks at $f = (2k-1)/(2\tau) = (2k-1)c/(4L)$ (quarter-wave).
  - $r < 0$: peaks at $f = k/\tau = k\,c/(2L)$ (half-wave, including a DC peak → DC blocker needed).
  - Published implementations used $|r| \approx 0.9$ (Sitt et al. 2010 used r = −0.9 with L = 1.9 cm). **Default here [D]: `trachea.reflection` r = +0.5, L = 33 mm, c = 350 m/s**; the sign is to be decided by listening (B2) and by fitting (E4).
- **Frequency-dependent loss [D]:** replace $r$ in the feedback path by $r\cdot \mathrm{LP}(z)$, a one-pole low-pass $\mathrm{LP}(z) = (1-a)/(1 - a z^{-1})$ with cutoff from `trachea.loss` (0 = no loss/22 kHz, 1 = cutoff 3 kHz). Guarantees $|r\,\mathrm{LP}| < 1$.
- **Implementation:** one circular delay line of length ≥ τ at $f_{int}$, two **fractional taps** (τ/2 output, τ feedback) using **3rd-order Lagrange interpolation** (safe under slow modulation of L by `body.size`; Laakso et al. 1996). At $f_{int}$ = 176.4 kHz and L = 33 mm, τ ≈ 33.3 samples.

### 4.7 Upper vocal tract: glottis – OEC – beak **[L]**

#### 4.7.1 Equivalent acoustic circuit

Acoustic–electric analogy: pressure ↔ voltage, volume flow ↔ current, air mass in a narrow tube ↔ inductance (inertance), cavity volume ↔ capacitance (compliance).

```
 p_t ──[ L_g ]──●──[ L_b ]──[ R_b ]── ground (outside air)
                │
              [ R_h ]
                │
              [ C_h ]
                │
             ground
```

- $L_g$: glottis inertance, $L_b$: beak inertance, $C_h$: OEC compliance, $R_h$: OEC wall losses, $R_b$: beak radiation resistance.
- Published state equations (as given in Arneodo et al. 2021, following earlier work of the Mindlin group, e.g. Perl et al. 2011), with input $V = p_t$ and output $p_{out} = R_b\,i_3$:

$$
\begin{aligned}
\dot i_1 &= \Omega_1\\
\dot\Omega_1 &= -\frac{1}{L_g C_h} i_1 - R_h\!\left(\frac{1}{L_b} + \frac{1}{L_g}\right)\Omega_1 + \left(\frac{1}{L_g C_h} - \frac{R_b R_h}{L_b L_g}\right) i_3 + \frac{1}{L_g}\dot V + \frac{R_h}{L_g L_b} V\\
\dot i_3 &= -\frac{L_g}{L_b}\Omega_1 - \frac{R_b}{L_b} i_3 + \frac{1}{L_b} V
\end{aligned}
$$

- **[V]** Its frequency response is identical (to machine precision) to the circuit above:

$$
H_{uvt}(s) = \frac{Z_p}{sL_g + Z_p}\cdot\frac{R_b}{sL_b + R_b},\qquad Z_p = \frac{Z_c Z_b}{Z_c + Z_b},\ Z_c = R_h + \frac{1}{sC_h},\ Z_b = sL_b + R_b .
$$

#### 4.7.2 Implementation form (no input derivative) **[D, equivalent]**

Use physical states $\mathbf{q} = [\,i_g,\ v_c,\ i_b\,]^T$ (glottis flow, OEC capacitor pressure, beak flow). With $v_n = v_c + R_h(i_g - i_b)$ the pressure at the OEC node:

$$
\begin{aligned}
L_g\,\dot i_g &= p_t - v_c - R_h\,i_g + R_h\,i_b\\
C_h\,\dot v_c &= i_g - i_b\\
L_b\,\dot i_b &= v_c + R_h\,i_g - (R_h + R_b)\,i_b
\end{aligned}
\qquad\Longrightarrow\qquad \dot{\mathbf q} = \mathbf A\,\mathbf q + \mathbf b\,p_t
$$

Discretise with the **trapezoidal rule** (bilinear, A-stable, modulation-friendly):

$$ \mathbf q[n+1] = \big(\mathbf I - \tfrac{h}{2}\mathbf A\big)^{-1}\Big[\big(\mathbf I + \tfrac{h}{2}\mathbf A\big)\mathbf q[n] + \tfrac{h}{2}\mathbf b\,\big(p_t[n] + p_t[n+1]\big)\Big], \quad h = 1/f_{int} $$

Recompute the two 3×3 matrices whenever parameters change (at most once per control sub-block). Optional frequency pre-warping at $f_H$ is unnecessary at $f_{int} \ge 176$ kHz.

Outputs available without numerical differentiation:
- beak flow $U_b = i_b$ (published output ∝ $R_b i_b$),
- **radiated pressure** $p_{rad} \propto \dot i_b = \big(v_c + R_h i_g - (R_h+R_b) i_b\big)/L_b$ (§2.6).

#### 4.7.3 Physical parametrisation **[L formulas, D values]**

With air density $\rho = 1.15$ kg/m³ and $c = 350$ m/s:

$$ L = \frac{\rho\,\ell}{A}\ (\text{tube of length } \ell,\ \text{area } A), \qquad C_h = \frac{V_{oec}}{\rho c^2}, \qquad f_H = \frac{1}{2\pi\sqrt{C_h\,L_{eq}}},\quad L_{eq} = \frac{L_g L_b}{L_g + L_b} $$

- Glottis: $\ell_g = 2$ mm, radius 0.4 mm → $L_g \approx 4.6\times10^3$ kg m⁻⁴.
- Beak: $\ell_b = 8$ mm, opening area $A_b = A_0 + g\,(A_1 - A_0)$, $A_0 = 1$ mm², $A_1 = 20$ mm², $g$ = `beak.gape` ∈ [0,1].
- **Control by resonance, not volume [D]:** musicians and the pitch-led controller set `oec.freq_hz` = $f_H$; the engine derives $C_h = 1/\big((2\pi f_H)^2 L_{eq}\big)$ (and reports the implied volume for sanity; e.g. $f_H$ = 3 kHz, gape 0.5 → ≈ 0.54 cm³).
- Damping via the resonator's characteristic impedance $Z_0 = \sqrt{L_{eq}/C_h}$: $R_h = z_h Z_0$, $R_b = z_b Z_0$; defaults $z_h$ = `oec.damping` = 0.1, $z_b$ = `beak.damping` = 0.3.
- **[V]** With radiation (derivative of beak flow) the combined response has its main peak at $f_H$; flow output alone is low-pass with a hump at $f_H$. WP-B3 must measure and tabulate peak frequency and Q over the parameter ranges.

### 4.8 Output stage **[D]**

$$ y_{int}(t) = \mathrm{DCblock}\Big\{(1 - \rho_{rad})\,\hat U_b(t) + \rho_{rad}\,\hat{\dot U_b}(t)\Big\}\cdot 10^{\,\text{gain}_{dB}/20} $$

- `out.radiation` = $\rho_{rad}$ ∈ [0,1], default 1. Hats denote fixed calibration gains (measured in B3) so both paths have comparable loudness for a reference note.
- Then anti-alias **decimation** to the host rate (§6.1). The plugin adds a transparent safety soft-clipper at the very end (C1).

### 4.9 Body size scaling **[D]**

`body.size` = σ ∈ [0.25, 4], σ = 1 ≈ zebra finch. All acoustic time scales scale together, so the bird gets bigger instead of being pitch-shifted:

$$ \gamma = \gamma_0/\sigma, \quad L = L_0\,\sigma, \quad f_H = f_{H}^{(0)}/\sigma, \quad \text{beak and glottis dimensions} \propto \sigma $$

In performance mode the played pitch is preserved by solving β for the new γ (§7.4); size then mostly changes timbre, comfortable register and articulation speed.

### 4.10 Default Tier-1 parameter set (starting point, not truth)

| Parameter | Default | Range | Tag |
|---|---|---|---|
| γ₀ | 24 000 | 20 000 – 26 000 (advanced) | [L] |
| α_rest / α_max | −0.10 / 0.45 | – | [D] |
| β (per side) | 0.30 | −0.20 – 1.00 | [L] range, [D] default |
| τ_att / τ_rel | 8 ms / 15 ms | 1 – 100 ms | [D] |
| σ_β, f_η | 0.005, 300 Hz | 0 – 0.05, 50 – 2000 Hz | [L] idea, [D] values |
| a_asp | 0.02 | 0 – 1 | [D] |
| L (trachea) | 33 mm | 10 – 120 mm | [L] |
| r (reflection) | +0.5 | −0.95 – +0.95 | [D] |
| trachea loss | 0.3 | 0 – 1 | [D] |
| f_H (OEC) | 3000 Hz | 300 – 10 000 Hz | [L] role, [D] value |
| z_h / z_b | 0.1 / 0.3 | 0.01 – 1 | [D] |
| beak gape | 0.5 | 0 – 1 | [D] |
| radiation mix | 1.0 | 0 – 1 | [D] |

---

## 5. Tier-2 source (optional, WP-G2) **[L]**

A more physical labial model with the same interface as `LabialOscillator` (inputs: pressure, tension-like parameter; output: displacement). Useful for rougher, more "organic" regimes and as an expert engine. Not required for the product.

Physical flapping model (Laje, Gardner & Mindlin 2002; Mindlin & Laje 2005), using Titze's flapping approximation for the pressure acting on the labia (Titze 1988):

$$
\begin{aligned}
\dot x &= y\\
m\,\dot y &= -k(x)\,x - b(y)\,y - c\,x^2 y + a_{lab}\,p_s\,\frac{\Delta a + 2\tau y}{a_{01} + x + \tau y}
\end{aligned}
\qquad k(x) = k_1 + k_2 x^2,\quad b(y) = b_1 + b_2 y^2,\quad \Delta a = a_{01} - a_{02}
$$

- $a_{01}, a_{02}$: rest half-openings at the lower and upper edge of the labia; $\tau$: time for the surface wave to travel across the labia; $p_s$: subsyringeal pressure; $k_1$: stiffness (tension).
- The average pressure on the labia is $p_{avg} = p_s(1 - a_2/a_1)$ with $a_1 = a_{01} + x + \tau y$, $a_2 = a_{02} + x - \tau y$, which gives the fraction above.
- A two-mass model (Zaccarelli et al. 2006) is a further option for asymmetric/noisy regimes; out of scope unless G2 shows a clear musical gain.
- Parameter values must be taken from the cited papers by the agent executing G2 and checked by simulation (onset threshold vs pressure, frequency vs $k_1$).

---

## 6. Numerical methods and real-time design

### 6.1 Rates and oversampling **[V + D]**

- Internal rate: $f_{int} = OS \times f_s$, with **OS = smallest power of two such that $f_{int} \ge 176\,400$ Hz** (44.1/48 kHz → OS = 4; 88.2/96 kHz → OS = 2; ≥176.4 kHz → 1).
- The whole voice (§4.1) runs at $f_{int}$. Decimate to $f_s$ with a cascade of **half-band filters** (e.g. polyphase IIR half-band allpass pairs, or linear-phase FIR half-bands; ≥ 90 dB stopband above $f_s/2$, passband flat to 20 kHz within 0.1 dB). Report the latency (plugin must report it to the host if non-zero).
- Controls enter at host rate and are **linearly interpolated** to $f_{int}$.

### 6.2 Control signals

- Every physiological parameter has a smoother (one-pole, time constant per parameter in the registry; typical 1–5 ms for fast controls like β, 20 ms for anatomy constants).
- Fast control curves produced by the gesture engine are already smooth and are **not** additionally smoothed beyond 0.5 ms (would blunt bird articulation).
- Sub-block processing: max 32 host samples per control update.

### 6.3 Real-time safety rules

- No heap allocation, locks, file I/O or logging on the audio thread. Preallocate at `prepare()`.
- All loops bounded. Denormal protection (FTZ/DAZ flags on x86; on ARM add tiny DC/noise or check `std::fpclassify`).
- All parameter values clamped to registry ranges before use.
- Non-finite detection per block → reset voice state + flag (counted in tests; must stay 0).

### 6.4 Determinism

- All noise generators use a **seedable PRNG** (e.g. xoshiro128+), seeded per voice. Offline renders (CLI, Python, fitting) must be **bit-reproducible** for a given seed, parameter file and sample rate. Fitting uses fixed seeds (or noise off).

### 6.5 CPU budget

One Tier-1 voice at $f_{int}$ ≈ 176 kHz: 2 RK4 oscillators (8 evaluations of a cubic polynomial), a delay line with 2 Lagrange taps, a 3×3 state update, a few one-poles, plus decimation. Rough estimate: < 100 flops/internal sample → < 20 M flops/s → **target < 0.5 % of one core per voice** (Apple Silicon / recent x86), verified by benchmark (B6). Polyphony target: 16 voices.

---

## 7. Playability concept

### 7.1 The problem

1. Birds change pitch **faster than any human can play** (sweeps of kHz in tens of ms, trills of 20–40 notes/s).
2. Physiological controls are **coupled**: pressure changes pitch and timbre; tension changes pitch non-linearly; the sound can disappear (below threshold).
3. Musicians need **tuning**, **repeatability**, **expressive continuous control**, and **host tempo**.

**Principle:** the musician plays the *intention* (pitch centre, breath, how agitated/free the bird is); the engine produces the *fast detail* (gestures) and solves the physics so that the intention is respected.

### 7.2 Three control layers **[D]**

| Layer | Who touches it | Content | Where |
|---|---|---|---|
| **3. Performance (musical)** | every musician | ~8 macros, MIDI/MPE, presets, voice bank choice | Perform page |
| **2. Gestures** | advanced users only (hidden by default) | gesture library (learned + archetypes), syllable grammar, editing/drawing | Gesture page (F4) |
| **1. Physiology** | experts / sound designers | pressure, tension per side, gates, trachea, OEC, beak, size, noise | Physiology page |

Gestures are **generated** by the engine from the voice bank; by default users never see them. Advanced users can inspect, edit, draw and save them.

### 7.3 Pitch-led voice controller **[D]**

For each voice, per host sample (or sub-block), in this order:

1. **Target pitch** $P(t)$ in cents (absolute, 0 = MIDI note 0): combines played note, glide, pitch bend/MPE, generated gesture pitch (Leash §7.6, gestures §7.7–7.8), Snap (§7.5).
2. **Dependent curves** derived from pitch and gesture: amplitude $E(t)$ (dB), tone $T(t) \in [0,1]$ (rich ↔ pure), side $S(t) \in [-1, 1]$ (left ↔ right), OEC resonance, beak gape.
   - amplitude–pitch coupling: $E \mathrel{+}= k_{ap}\,(P - P_{anchor})/1200$ dB/octave (default $k_{ap}$ = 0, learned in F2),
   - **OEC tracking** (Riede et al. 2006): $f_H = f_{H,base}\,(f_0/f_{ref})^{\kappa}$, $\kappa$ = `perf.oec_tracking` ∈ [0,1], default 0.7,
   - **gape tracking** (Hoese et al. 2000): $g = g_{base} + \kappa_g\,\log_2(f_0/f_{ref})$, clamped [0,1], default $\kappa_g$ = 0.15.
3. **Body:** γ from `body.size` (§4.9).
4. **Physiological solve** using the atlas (§7.4): $(\nu, T, \text{Breath}) \to (\alpha, \beta)$ with $\nu = 2\pi f_0/\gamma$.
5. **Sides:** $S(t)$ → gates $g_L, g_R$ (equal-power crossfade), per-side β; the inactive side is pre-tuned to the pitch it would need; a jump larger than `perf.side_switch_cents` triggers a switch.
6. Physiological controls → `BirdVoice` (§4).

### 7.4 The atlas and the inverse map **[D]**

**Forward source atlas** (computed offline once, WP-C2), at $\gamma_0$, on a grid $\alpha \in [-0.1, 0.6]$ (≥ 141 points), $\beta \in [-0.3, 1.2]$ (≥ 301 points):
- normalised frequency $\nu = 2\pi f_0/\gamma_0$ (dimensionless; valid for any γ because frequencies scale with γ),
- peak-to-peak amplitude of $x$, SCI of $x$, regime label (silent / periodic / period-doubled / irregular),
- **onset boundary** $\alpha_{on}(\beta)$ found by ramping α upward from rest, **offset boundary** $\alpha_{off}(\beta)$ by ramping downward (hysteresis).

**Inverse map** used at runtime:
- $\beta = B(\alpha, \nu)$: for each α row, invert the monotone relation ν(β) by interpolation; unreachable → flagged.
- **Tone parametrisation:** for a target ν the admissible (α, β) form an iso-frequency curve. Parametrise it by α between $\alpha_{lo}(\nu) = \alpha_{on}$ + margin δ (tone = 0, richest, closest to threshold) and $\alpha_{hi}(\nu)$ (tone = 1, purest, limited by $\alpha_{max}$ or $\beta_{max}$). $\alpha_T = \alpha_{lo} + T\,(\alpha_{hi} - \alpha_{lo})$.
- **Physics control** (`perf.physics` = φ): $\alpha = (1-\varphi)\,\alpha_T + \varphi\,\alpha_{breath}$, then clamped into $[\alpha_{lo}, \alpha_{hi}]$ while the note is held. With φ = 1, breath changes timbre like a real bird (still in tune, because β is solved after α).
- **Note end:** α is driven below $\alpha_{off}$ with the respiratory release time → physical offset.
- **Out of range pitch:** soft-clamp to the nearest reachable ν and expose a "range" indicator in the UI; option to fold by octaves (`perf.range_fold`).
- Runtime lookup: bilinear interpolation in precomputed tables (no search on the audio thread). Tables are generated into a C++ header or a binary asset (see `02_architecture.md` §9.3).
- **Tuning accuracy target:** ≤ 5 cents steady-state error for reachable pitches at tone ∈ [0.2, 1]; ≤ 15 cents at tone < 0.2 (near threshold, noise-sensitive).

### 7.5 Macros (Perform page) **[D]**

| Macro | ID | Range | Effect (summary) |
|---|---|---|---|
| **Leash** | `perf.leash` | 0–1 | 0: pitch follows the player (bird-voiced theremin). 1: the bird sings its own syllables around the held note. |
| **Motion** | `perf.motion` | 0–1 | amount and rate of pitch movement ("dynamism"): ornament depth/rate in direct mode; gesture choice (smooth ↔ trills/sweeps) and density of events in generative mode. |
| **Range** | `perf.range` | 0–2 | scales pitch excursions of gestures (1 = natural). |
| **Speed** | `perf.speed` | 0.25–4 | time scale of gestures and syllables; optional tempo sync. |
| **Variation** | `perf.variation` | 0–2 | 0: prototypical gestures; 1: natural variability of the real bird; 2: exaggerated/invented. |
| **Breath** | `perf.breath` | 0–1 | air-sac pressure command; loudness, onset character; with Physics also timbre. Velocity/aftertouch/MPE pressure modulate it. |
| **Snap** | `perf.snap` | 0–1 | pulls stable parts of the pitch curve to the played note / scale; sweeps stay free. |
| **Size** | `body.size` | 0.25–4 | coherent anatomical scaling (§4.9). |

Secondary (Perform page, smaller): **Tone** `perf.tone`, **Physics** `perf.physics`, **Density** `perf.density`, **Glide** `perf.glide_ms`, **Scale/Root** `perf.scale`, `perf.root`, **Sync** `perf.tempo_sync`, **Voice bank** `perf.voice_bank`, **Polyphony** `perf.polyphony`.

**Snap definition:** let $P_g(t)$ be the gesture pitch and $Q(\cdot)$ quantisation to the nearest allowed pitch (played note or scale degree). Stability weight $w(t) = \exp(-|\dot P_g|/v_0)$ with $v_0$ = 2000 cents/s. $P = P_g + \text{snap}\cdot w\,(Q(P_g) - P_g)$.

**MIDI (default mapping):** note-on → voice start, anchor pitch, velocity → initial Breath boost; channel/poly aftertouch, MPE pressure → Breath; pitch bend (±2 st default) / MPE X → manual pitch offset; MPE Y (CC74) → Tone; mod wheel (CC1) → Motion; sustain (CC64) → hold. Voice modes: mono legato (default for single bird), poly (flock).

### 7.6 Leash: direct ↔ generative **[D]**

- **Direct component** $D(t)$: note pitch + glide + bend + *ornaments* scaled by Motion: onset scoop (glide-in from −Range·300 cents over 15–40 ms), fast vibrato/trill-like FM (rate 15–35 Hz × Speed, depth ∝ Motion·Range), end fall.
- **Generative component** $G(t)$: gesture engine output (§7.8) anchored on the held note.
- Pitch: $P(t) = (1-\lambda)\,D(t) + \lambda\,G(t)$, λ = Leash (crossfade in cents, both curves continuous).
- Amplitude/silences: $E(t) = E_{player}(t) \cdot \big[(1-\lambda) + \lambda\,E_{gen}(t)\big]$ — at high Leash the bird articulates its own syllables and breaths while the player still controls overall breath.
- Latency: no look-ahead. Gestures start at note-on and move *towards* the target; this matches bird articulation.

### 7.7 Gestures, syllables, grammar **[D, inspired by L]**

Motor gestures: the Mindlin group showed that the physiological parameter trajectories of canary and zebra finch song decompose into a small number of smooth, simple segments ("gestures"), with boundaries at extrema of the trajectories (Gardner et al. 2001; Amador et al. 2013). We adopt this at the **musical layer**:

- **Gesture** = short segment (10–200 ms) described on normalised time $u \in [0,1]$ by N = 64 samples of: pitch (cents relative to the gesture anchor), amplitude (dB relative to the gesture peak), tone (0–1), side (−1..1); plus duration (ms) and anchor offset (cents relative to the syllable anchor).
- **Syllable** = sequence of gestures (+ internal micro-gaps).
- **Song/phrase** = sequence of syllables and gaps, generated by a **grammar**.

### 7.8 Variation generation **[D]**

- **Functional PCA per gesture type** (Ramsay & Silverman 2005): concatenate the registered curves into a vector $\mathbf c \in \mathbb R^{3N}$ (pitch, amplitude, tone, each standardised), learn mean $\boldsymbol\mu$, components $\boldsymbol\phi_k$, variances $\lambda_k$ (keep K modes explaining ≥ 95 % variance, K ≤ 8).
- Generate: $\mathbf c = \boldsymbol\mu + \sum_k z_k \boldsymbol\phi_k$, $z_k = v\,\sqrt{\lambda_k}\,\xi_k$, $\xi_k \sim \mathcal N(0,1)$ (truncated at ±2.5), $v$ = Variation. Durations: log-normal per type, scaled by Speed. At $v > 1$ additionally interpolate towards a randomly chosen other type's mean with weight $(v-1)\cdot 0.3$ ("invention").
- **Range** multiplies the pitch part after generation; **Motion** biases the choice of gesture types (sorted by a "movement" score = RMS pitch velocity) and the number of gestures per syllable.
- **Grammar:** first-order Markov chain over syllable types, with a start state (introductory notes) and learned transition probabilities $P_{ij}$; Variation smooths it: $P' = (1 - 0.2v)P + 0.2v\,U$ ($U$ uniform over observed types). Gaps: log-normal, scaled by Density and Speed; tempo sync optionally quantises syllable onsets.
- All sampling uses a per-voice seeded PRNG; a "seed" parameter allows reproducible performances; a "freeze" button keeps the current phrase repeating.

### 7.9 Archetypes: songbird sound types (hand-designed) **[D]**

Available before any fitting (C4), later complemented by learned zebra finch banks. Each archetype is a parametric generator of gesture curves:

| Archetype | Pitch curve | Tone | Side | Parameters | Inspiration |
|---|---|---|---|---|---|
| Whistle | flat or gentle arch | 0.8–1 | one | duration, arch depth | many songbirds' pure notes |
| Upsweep / Downsweep | sigmoid or exponential | 0.6–1 | one (switch if > 1 octave) | depth, curvature, duration | sparrows, cardinal-like sweeps |
| Arch / U | rise-fall / fall-rise | 0.6–1 | one | depth, asymmetry | – |
| Trill | repeated micro-sweeps at rate R | 0.5–0.9 | one | rate 8–40 Hz, element depth, count | canary tours |
| Buzz | low, slightly wobbling | 0–0.15 + tension noise ↑ | one or both | roughness, duration | zebra finch noisy notes |
| Harmonic stack | low (500–900 Hz), nearly flat | 0.05–0.3, OEC tuned to a high harmonic | one | f0 offset, OEC harmonic | zebra finch stacks |
| Chip / tet | 10–30 ms fast downsweep | 0.4–0.8 | one | depth, sharpness | calls |
| Two-voice note | two independent pitches | any | both | interval (cents), beat | two-voice songbird notes |
| Side-switch sweep | broad sweep split across sides | 0.7–1 | L→R or R→L | depth, crossover pitch | cardinal-like |
| Intro note | short harmonic downsweep | 0.2–0.5 | one | depth, duration | zebra finch introductory notes |

---

## 8. The inverse problem: fitting real zebra finch song

### 8.1 Problem statement

Given a recording $y(t)$ of a syllable (or motif), find time-varying controls
$\boldsymbol\theta(t) = [\,\alpha(t),\ \beta_L(t),\ \beta_R(t),\ g_L(t),\ g_R(t),\ f_H(t),\ g_{beak}(t),\ E(t)\,]$
and per-bird constants
$\boldsymbol\phi = [\,\sigma\ (\text{or } \gamma),\ L,\ r,\ \text{loss},\ z_h,\ z_b,\ \text{gains}\,]$
such that $\mathrm{synth}(\boldsymbol\theta, \boldsymbol\phi) \approx y$ perceptually and in spectro-temporal features. First passes use **one active side** per segment (gate curve free), two-voice fitting is attempted only where the data analysis flags it.

### 8.2 Why frame-by-frame fitting is hard

1. **Ambiguity:** many (α, β, tract) combinations give similar spectra; source richness and tract filtering trade off.
2. **Vanishing signal:** below threshold the output is silent → no gradient/feedback about how far from onset we are.
3. **Phase:** waveform losses are useless (phase depends on history and noise) → magnitude-based losses only.
4. **Memory:** the oscillator and tract carry state across frames; a frame's result depends on the past.
5. **Nonlinearity/hysteresis:** small parameter changes near boundaries cause regime changes.

Hence a **pipeline** where each stage fixes the previous stage's weakness (all offline; compute time is not a constraint).

### 8.3 Pipeline overview

| Stage | WP | Input → Output | Method |
|---|---|---|---|
| 0 Preprocess | D1 | raw WAV + annotations → clean syllable clips + manifest | high-pass, optional spectral denoise, normalisation, segmentation |
| 1 Features | D2 | clip → frame features (1 ms hop) | f0, harmonic amplitudes, envelope, entropy, SCI, SAP features |
| 2 Voice atlas | E2 | engine → table of steady-state features over (ν, tone, $f_H$, gape) | offline brute-force rendering |
| 3 Initial path | E2 | features + atlas → θ₀(t) | k-NN candidates per frame + **Viterbi** smoothing |
| 4 Refinement | E3 | θ₀ → θ*(t) | B-spline controls, overlapping windows, **CMA-ES** on the real C++ engine, spectral losses + smoothness |
| 5 Bird constants | E4 | many syllables → φ* | alternating optimisation (controls fixed ↔ constants fixed) |
| 6 Gestures | F1–F2 | θ*(t) → gesture library, grammar, voice bank | segmentation at trajectory extrema, DTW, clustering, FPCA, Markov chain |

### 8.4 Stage details

**Stage 1 — features (per frame, hop 1 ms)** — all computed identically on recordings and on synthetic renders (one shared Python module):
- $f_0$: harmonic-template / subharmonic-summation pitch estimator with YIN/pYIN as secondary (de Cheveigné & Kawahara 2002; Mauch & Dixon 2014); search range 300–4000 Hz (configurable); voicing probability.
- Goodness of pitch (cepstral peak), **Wiener entropy** (log geometric/arithmetic mean of the power spectrum), spectral centroid, **SCI = centroid / f₀**, FM and AM as in Sound Analysis Pro (Tchernichovski et al. 2000).
- RMS level (dB), harmonic amplitudes $h_1..h_{16}$ (dB, relative to max), smoothed spectral envelope (cepstral, 20 coefficients).
- **Validation on synthetic renders with known ground truth** (f0 error target: median < 10 cents on voiced frames of tonal renders).

**Stage 2 — voice atlas.** Render short steady-state snippets (e.g. 80 ms, analyse the last 40 ms) of the **full Tier-1 voice** on a grid: ν (64–128 log-spaced points covering reachable pitch), tone (12–16), $f_H$ (24–32 log-spaced 0.6–10 kHz), gape (3–5). Store features of Stage 1 (harmonic amplitudes relative to max, entropy, SCI). Tens of thousands of renders: minutes to hours offline, parallel.

**Stage 3 — initial path (Viterbi).** For each voiced frame $n$: candidate set $\mathcal C_n$ = K (≈ 64) atlas entries with $|f_0$ mismatch$| < 50$ cents, ranked by emission cost
$e_n(c) = \sum_j w_j\,\big(F_j^{rec}[n] - F_j^{atlas}(c)\big)^2$.
Transition cost $t(c, c') = w_T\,(T - T')^2 + w_H\,(\log f_H - \log f_H')^2 + w_g\,(g - g')^2$. Dynamic programming (Viterbi) finds the minimum total cost path. Unvoiced/silent frames: α below threshold, other controls interpolated. Output: θ₀ at 1 ms resolution.

**Stage 4 — refinement on the full dynamics.**
- Controls parametrised as cubic B-splines (knot spacing 5 ms; 2.5 ms around onsets).
- **Pitch locking trick:** instead of fitting β directly, fit $\nu$-offset $\delta(t)$ (cents) and set $\beta(t) = B(\alpha(t), \nu_{rec}(t)\cdot 2^{\delta(t)/1200})$ with the inverse map. f₀ stays near the measured one by construction and the search space shrinks.
- Windows of 40 ms with 20 ms overlap. For each window: render from a warm-up point 30 ms earlier using the previous solution (carries oscillator/tract state), optimise the window's spline coefficients with **CMA-ES** (Hansen 2016; population 16–32, ≤ 300 generations, σ₀ from Stage 3 uncertainty), then blend overlapping solutions (Hann-weighted coefficient average).
- **Loss** on the window:
$$
\mathcal L = w_{S}\,\mathcal L_{MRSTFT} + w_{f}\,\overline{(\Delta\text{cents})^2}_{voiced} + w_{E}\,\overline{(\Delta\text{dB})^2} + w_{h}\,\overline{\|\Delta h\|^2} + w_{W}\,\overline{(\Delta\text{entropy})^2} + w_{sm}\sum \|\Delta^2 \text{coeff}\|^2
$$
  with the multi-resolution STFT loss (spectral convergence + log-magnitude L1 over FFT sizes 256/512/1024/2048 at 44.1 kHz, hop 25 %; Yamamoto et al. 2020).
- A final global polish over the whole syllable (all coefficients, small σ). Parallelise over windows of *different* syllables (not within a syllable, because of state carry).

**Stage 5 — bird constants.** Choose ~10 representative syllables covering the bird's note types. Alternate: (a) fix θ, optimise φ with CMA-ES over the summed loss; (b) fix φ, re-run Stage 4 warm-started. 2–4 iterations, stop when improvement < 2 %. Also compare trachea reflection sign hypotheses (r > 0 vs r < 0) explicitly.

**Stage 6 — gestures.** See §7.7–7.8 and WP-F1/F2. Round-trip requirement: gesture curves → pitch-led controller → engine must stay close (loss within +20 %) to the direct physiological resynthesis.

### 8.5 Identifiability benchmark (before touching real data)

Render known θ (from archetypes and random smooth curves), optionally add recording-like degradations (noise at 20–40 dB SNR, short reverb), fit, and report: control-curve errors, f0 error, feature errors, and loss. This benchmark is the regression test of the whole fitting pipeline (E1 creates it; E2–E4 must not degrade it).

### 8.6 Later options (backlog / G3)

- **Differentiable twin** (JAX) of Tier-1 for gradient-based refinement, validated against the C++ engine (golden tests).
- **Amortised inference:** train a neural network on synthetic renders to predict θ(t) and uncertainty from spectrograms (simulation-based inference), used as Stage 3 replacement; Stages 4–5 still polish.

---

## 9. Evaluation strategy

| What | Metric / method | Where |
|---|---|---|
| DSP correctness | analytic checks (bifurcation lines, resonance frequencies, filter responses), convergence tests | B1–B6 |
| Real-time | CPU per voice, worst-case block time, zero allocations (instrumented test), pluginval | B6, C1, G1 |
| Tuning | cents error over a scale at several tones and sizes | C2, C3 |
| Resynthesis quality | MRSTFT loss, f₀ error, envelope error, Wiener entropy error, harmonic amplitude error per syllable | E1–E4 |
| Perceptual proxy (optional) | cosine distance between embeddings of a bird-sound model (e.g. Google Perch, BirdNET), if available offline | E3, F2 |
| Listening | human listening at the end of each WP (A/B pages, "what to listen for"); ABX between real and resynthesised syllables | all |
| Musician usability | tasks: play a melody in tune; a trill; improvise with Leash 0→1; rate 1–5 | C6, F3, G1 |

---

## 10. Risks and open questions

| Risk | Impact | Mitigation |
|---|---|---|
| Zebra finch stacks live close to the onset boundary (sensitive, hysteretic) | unstable low notes, noise sensitivity | atlas with onset/offset boundaries, tone margin δ, tension noise tuned, fitting with pitch locking |
| Tracheal boundary conditions unknown (sign of r) | wrong formant structure | parameter, listening comparisons (B2), explicit hypothesis test in E4 |
| Source richness vs tract filtering trade-off | ambiguous fits | shared per-bird constants across syllables, smoothness priors, identifiability benchmark |
| Pitch-led compensation sounds "too clean" | less organic | Physics control, tension noise, ornaments, Variation |
| Inverse map discontinuities at regime boundaries | clicks, pitch jumps | tables smoothed, hysteresis-aware clamping, per-sample slew limits on α/β |
| Recording quality (cage noise, reverb, clipping) | biased features | preprocessing, robust losses, per-file quality score, exclusion list |
| Two-voice passages | poor single-side fits | detect via two-f₀ analysis in D2, flag, fit with both sides only where flagged |
| Narrow zebra finch palette | musical boredom | archetype library, Variation/invention, future banks from other songbirds |
| Polyphony CPU | dropouts | benchmarks, OS factor policy, SIMD later |
| JUCE licence terms | legal | decision D-003 (human check before release) |

---

## 11. References

- Amador A., Perl Y. S., Mindlin G. B., Margoliash D. (2013). Elemental gesture dynamics are encoded by song premotor cortical neurons. *Nature* 495, 59–64.
- Arneodo E. M., Perl Y. S., Goller F., Mindlin G. B. (2012). Prosthetic avian vocal organ controlled by a freely behaving bird based on a low dimensional model of the biomechanical periphery. *PLoS Comput. Biol.* 8, e1002546.
- Arneodo E. M., Chen S., Brown D. E., Gilja V., Gentner T. Q. (2021). Neurally driven synthesis of learned, complex vocalizations. *Current Biology* 31, 3419–3425.
- Boari S., Perl Y. S., Amador A., Margoliash D., Mindlin G. B. (2015). Automatic reconstruction of physiological gestures used in a model of birdsong production. *J. Neurophysiol.* 114, 2912–2922.
- Daley M., Goller F. (2004). Tracheal length changes during zebra finch song and their possible role in upper vocal tract filtering. *J. Neurobiol.* 59, 319–330.
- de Cheveigné A., Kawahara H. (2002). YIN, a fundamental frequency estimator for speech and music. *JASA* 111, 1917–1930.
- Elemans C. P. H. et al. (2015). Universal mechanisms of sound production and control in birds and mammals. *Nature Communications* 6, 8978.
- Fee M. S., Shraiman B., Pesaran B., Mitra P. P. (1998). The role of nonlinear dynamics of the syrinx in the vocalizations of a songbird. *Nature* 395, 67–71.
- Fletcher N. H. (1992). *Acoustic Systems in Biology*. Oxford University Press.
- Fletcher N. H., Riede T., Suthers R. A. (2006). Model for vocalization by a bird with distensible vocal cavity and open beak. *JASA* 119, 1005–1011.
- Frontiers in Neuroscience (2021). Synthetic birdsongs as a tool to induce, and listen to, replay activity in sleeping birds. doi:10.3389/fnins.2021.647978.
- Gardner T., Cecchi G., Magnasco M., Laje R., Mindlin G. B. (2001). Simple motor gestures for birdsongs. *Phys. Rev. Lett.* 87, 208101.
- Goller F., Suthers R. A. (1996). Role of syringeal muscles in controlling the phonology of bird song. *J. Neurophysiol.* 76, 287–300.
- Goller F., Larsen O. N. (1997). A new mechanism of sound generation in songbirds. *PNAS* 94, 14787–14791.
- Goller F., Mallinckrodt M. J., Torti S. D. (2004). Beak gape dynamics during song in the zebra finch. *J. Neurobiol.* 59, 289–303.
- Hansen N. (2016). The CMA Evolution Strategy: A Tutorial. arXiv:1604.00772.
- Hoese W. J., Podos J., Boetticher N. C., Nowicki S. (2000). Vocal tract function in birdsong production: experimental manipulation of beak movements. *J. Exp. Biol.* 203, 1845–1855.
- Laakso T. I., Välimäki V., Karjalainen M., Laine U. K. (1996). Splitting the unit delay. *IEEE Signal Processing Magazine* 13(1), 30–60.
- Laje R., Gardner T. J., Mindlin G. B. (2002). Neuromuscular control of vocalizations in birdsong: a model. *Phys. Rev. E* 65, 051921.
- Mauch M., Dixon S. (2014). pYIN: a fundamental frequency estimator using probabilistic threshold distributions. *ICASSP 2014*.
- Mindlin G. B., Laje R. (2005). *The Physics of Birdsong*. Springer.
- Ohms V. R., Snelderwaard P. C., ten Cate C., Beckers G. J. L. (2010). Vocal tract articulation in zebra finches. *PLoS ONE* 5, e11923.
- Perl Y. S., Arneodo E. M., Amador A., Goller F., Mindlin G. B. (2011). Reconstruction of physiological instructions from zebra finch song. *Phys. Rev. E* 84, 051909.
- Ramsay J. O., Silverman B. W. (2005). *Functional Data Analysis*, 2nd ed. Springer.
- Riede T., Suthers R. A., Fletcher N. H., Blevins W. E. (2006). Songbirds tune their vocal tract to the fundamental frequency of their song. *PNAS* 103, 5543–5548.
- Sitt J. D., Arneodo E. M., Goller F., Mindlin G. B. (2010). Physiologically driven avian vocal synthesizer. *Phys. Rev. E* 81, 031927.
- Smith J. O. *Physical Audio Signal Processing* (online book, CCRMA, Stanford).
- Suthers R. A. (1990). Contributions to birdsong from the left and right sides of the intact syrinx. *Nature* 347, 473–477.
- Suthers R. A., Goller F. (1997). Motor correlates of vocal diversity in songbirds. *Current Ornithology* 14, 235–288.
- Suthers R. A., Zollinger S. A. (2004). Producing song: the vocal apparatus. *Ann. N. Y. Acad. Sci.* 1016, 109–129.
- Tchernichovski O., Nottebohm F., Ho C. E., Pesaran B., Mitra P. P. (2000). A procedure for an automated measurement of song similarity. *Animal Behaviour* 59, 1167–1176.
- Titze I. R. (1988). The physics of small-amplitude oscillation of the vocal folds. *JASA* 83, 1536–1552.
- Westneat M. W., Long J. H., Hoese W., Nowicki S. (1993). Kinematics of birdsong: functional correlation of cranial movements and acoustic features in sparrows. *J. Exp. Biol.* 182, 147–171.
- Yamamoto R., Song E., Kim J.-M. (2020). Parallel WaveGAN: a fast waveform generation model based on generative adversarial networks with multi-resolution spectrogram. *ICASSP 2020*.
- Zaccarelli R., Elemans C. P. H., Fitch W. T., Herzel H. (2006). Modeling bird songs: voice onset, overtones and registers. *Acta Acustica united with Acustica* 92, 741–748.
- Zann R. A. (1996). *The Zebra Finch: A Synthesis of Field and Laboratory Studies*. Oxford University Press.
- Zavalishin V. *The Art of VA Filter Design* (free book, Native Instruments).

> Note for agents: page/volume numbers were compiled from memory and search results while preparing this document; if an exact figure or equation from a paper matters for your WP, check the primary source and record what you checked in your report.
