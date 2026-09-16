# WP-B5 — Two sources: left/right labia, gating, side switching

| Field | Value |
|---|---|
| Phase | B — The bird body |
| Depends on | B4 |
| Size | M |
| Milestone | – |

## 1. Goal

Give the syrinx its two independent sides. Each side has its own tension, gate and α offset, shares the air-sac pressure, and feeds the common trachea. Add optional weak coupling between the sides and a **side-switching** utility that realises large pitch jumps without glides (used by the pitch-led controller in C3). This unlocks two-voice notes and bird-like jumps.

## 2. Read first

- `01_research.md` §2.3 (two voices, side switching), **§4.4**, §4.5
- `02_architecture.md` §6, §7.1
- Reports of WP-B4, WP-D1 (observations on two-voice passages)

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/model/bilateral_source.hpp` | `BilateralSource` (two oscillators, gating, coupling, source mix) |
| `core/include/cuicui/control/side_switcher.hpp` | `SideSwitcher` utility |
| `core/include/cuicui/model/bird_voice.hpp` | uses `BilateralSource` |
| `params.def` | add `src.R.beta`, `src.R.alpha_offset`, `src.L.gate`, `src.R.gate`, `src.coupling` |
| `core/tests/test_bilateral_source.cpp`, `test_side_switcher.cpp` | tests §7 |
| `python/cuicui/analysis/locking.py` | coupling/locking analysis plot |

## 4. Specification

### 4.1 Bilateral source

- Per side s ∈ {L, R}: $\alpha_s = \alpha_{base} + \alpha^{off}_s - (1 - g_s)\cdot 0.6$, $\beta_s^{eff} = \beta_s + \sigma_\beta\eta_s$.
- Oscillator equations as in B1, plus coupling term when ε = `src.coupling` > 0:
  $\dot y_s \mathrel{+}= \varepsilon\,\gamma^2\,(x_{s'} - x_s)$ with $s'$ the other side.
- **Integration:** when ε > 0 the two sides form one 4-state system and must be advanced by **one joint RK4 step** (the coupling term uses the other side's intermediate stage values). When ε = 0 the joint step reduces exactly to two independent steps (verify bit-identical).
- Source mix: $s = G\,(g_L\tilde x_L + g_R\tilde x_R) + n_{asp}$, with $\bar x$ for aspiration = $(g_L\tilde x_L + g_R\tilde x_R)/\max(g_L + g_R, 10^{-3})$.
- Each side has its own DC blocker. Closed sides keep integrating (so reopening gives a physical onset).
- R tension noise uses the stream reserved in B4.

### 4.2 Side switcher (control utility, no audio)

```cpp
namespace cuicui::control {
struct SideTargets { double betaL, betaR, gateL, gateR; };
class SideSwitcher {
public:
    void prepare(double controlRate);
    void setCrossfadeMs(double ms);            // 2–6 ms typical
    void setActive(int side);                  // 0 = L, 1 = R: starts an equal-power crossfade
    // betaForPitchL/R: tension each side would need for the current target (provided by caller)
    SideTargets next(double betaForPitchL, double betaForPitchR);
};
}
```

- Equal-power crossfade of gates: $g_{new} = \sin(\tfrac{\pi}{2}u)$, $g_{old} = \cos(\tfrac{\pi}{2}u)$, u: 0 → 1 over the crossfade.
- The inactive side's β always tracks the value provided for its side (pre-tuning), so when it opens it is already at the right pitch.
- In this WP the caller provides β values directly (C3 will provide them from the inverse map).

## 5. Suggested steps

1. `BilateralSource` with ε = 0; regression test vs B4 (left-only).
2. Joint RK4 with coupling; locking analysis.
3. `SideSwitcher` + tests.
4. Listening renders (use render specs with automation of gates and β; for switch demos write a tiny C++ or Python driver that uses `SideSwitcher`).

## 6. Out of scope

Pitch-to-β mapping (C2/C3), automatic decision of when to switch (C3), per-side trachea (not anatomical: sides share the trachea).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Regression: `src.R.gate` = 0, ε = 0 → output bit-identical to the B4 voice for the same seed and parameters (golden renders from B4). |
| 2 | Closed side: L gate 0, R gate 1 at a note; energy attributable to L (render with R gate 0 too) < −80 dB relative. |
| 3 | Two voices, ε = 0: L at (α 0.2, β 0.2) and R at (α 0.2, β 0.5): spectrum of the source mix (before trachea) contains both fundamentals (±0.5 % of single-side values); combination tones f_R ± f_L < −60 dB relative to the fundamentals. |
| 4 | Coupling: with β_L = 0.30, β_R = 0.30 + Δ, report the largest Δ that still locks (single f₀) for ε ∈ {0.01, 0.03, 0.1}; plot. (No pass threshold; must be monotone in ε.) |
| 5 | Joint RK4 with ε = 0 equals independent stepping bit-for-bit. |
| 6 | Side switch without glide: L at ≈ 1.5 kHz, R pre-tuned at ≈ 3.5 kHz, crossfade 4 ms: short-time f₀ estimates (2 ms windows) never stay in 1.8–3.2 kHz for more than 2 consecutive windows; total transition ≤ 8 ms. |
| 7 | Reopening a closed side produces a physical onset: no sample-to-sample jump larger than 3× the steady-state maximum jump. |
| 8 | Fuzz 30 s (all source params incl. gates and coupling): no non-finite values. Zero allocations. `scripts/check.sh` passes. |

## 8. Listening & results

Render into `listening/B5/` (48 kHz, full chain).

| File | Content | Listen for |
|---|---|---|
| `01_left_only.wav`, `02_right_only.wav`, `03_both_two_voice.wav` | L 1.8 kHz, R 2.4 kHz (~perfect fourth), group "two voices" | 03: two independent pitches at once, not a chord of harmonics |
| `04_beating.wav` | L and R detuned by 8 Hz, ε = 0 | slow beating |
| `05_coupling_sweep.wav` | same detuning, ε 0 → 0.1 over 5 s | beating disappears when the sides lock |
| `06_jump_glide_single_side.wav` vs `07_jump_side_switch.wav` | 1.5 kHz ↔ 3.5 kHz jumps every 250 ms: one side gliding with β vs side switching | 07: clean jumps, no swoop in between |
| `08_octave_trill_switch.wav` | fast alternation between sides at 12 Hz, octave apart | crisp two-level trill |
| `09_broad_downsweep_split.wav` | downsweep 6 → 1.2 kHz over 150 ms, R covers the top, L the bottom, crossover at 3 kHz | continuous broad sweep |

Figures: locking plot; spectrograms of 06 vs 07 and 09.

## 9. Pitfalls

- Coupling inside RK4 must use stage-consistent values of both sides; coupling computed from the previous sample's other-side value is wrong and will fail test #5 with ε > 0 comparisons.
- Keep the R noise stream reserved in B4; otherwise golden renders change (test #1).
