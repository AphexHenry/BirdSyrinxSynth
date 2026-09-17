# state.md — live project state

> Updated by agents at the start and end of every WP (see `protocol.md` §2.4 and §5.5). The human may edit it at any time (e.g. change order, mark a WP `optional` or `todo`).

**Last updated:** 2026-09-17 — by: WP-B5 agent

## Status legend

| Status | Meaning |
|---|---|
| `todo` | not started; eligible when dependencies are `done` |
| `in progress` | an agent is working on it (or a session was interrupted) |
| `awaiting approval` | finished, waiting for the human's approval to commit |
| `partial` | committed checkpoint; remaining work listed in its report |
| `done` | approved and committed (tag `wp-<ID>`) |
| `blocked` | cannot proceed; reason in "Open questions" |
| `optional` | not selected automatically; human sets it to `todo` to schedule it |

## Execution table

| # | WP | Title | Depends on | Status | Updated | Tag | Spec |
|---|---|---|---|---|---|---|---|
| 1 | A1 | Repository, build system, render CLI, listening kit | – | done | 2026-09-16 | wp-A1 | `work_packages/WP-A1_repo_build_render_listening.md` |
| 2 | A2 | Parameter registry, control signals, oversampling, Python bindings | A1 | done | 2026-09-16 | wp-A2 | `work_packages/WP-A2_params_oversampling_bindings.md` |
| 3 | D1 | Zebra finch dataset ingestion & catalogue | A1 | optional | 2026-09-16 | | `work_packages/WP-D1_dataset_ingestion.md` |
| 4 | B1 | Labial oscillator (normal form) | A2 | done | 2026-09-16 | wp-B1 | `work_packages/WP-B1_labial_oscillator.md` |
| 5 | B2 | Trachea waveguide | B1 | done | 2026-09-17 | wp-B2 | `work_packages/WP-B2_trachea.md` |
| 6 | B3 | Upper vocal tract: glottis–OEC–beak + radiation | B2 | done | 2026-09-17 | wp-B3 | `work_packages/WP-B3_upper_vocal_tract.md` |
| 7 | D2 | Feature extraction (shared real/synthetic) | D1, B3 | todo | | | `work_packages/WP-D2_features.md` |
| 8 | B4 | Respiratory dynamics & noise sources | B3 | done | 2026-09-17 | wp-B4 | `work_packages/WP-B4_respiration_noise.md` |
| 9 | B5 | Two sources: left/right, gating, side switching | B4 | done | 2026-09-17 | wp-B5 | `work_packages/WP-B5_two_sources.md` |
| 10 | B6 | Voice assembly, body size, state save/load, benchmarks | B5 | todo | | | `work_packages/WP-B6_voice_assembly.md` |
| 11 | C1 | Plugin shell + offline plugin renderer | B6 | todo | | | `work_packages/WP-C1_plugin_shell.md` |
| 12 | C2 | Source atlas & inverse pitch map | B6 | todo | | | `work_packages/WP-C2_atlas_inverse_map.md` |
| 13 | C3 | Pitch-led direct playing | C1, C2 | todo | | | `work_packages/WP-C3_pitch_led_playing.md` |
| 14 | C4 | Archetype library + gesture score format & player | C3 | todo | | | `work_packages/WP-C4_archetypes_gesture_score.md` |
| 15 | C5 | Generative performance engine | C4 | todo | | | `work_packages/WP-C5_generative_engine.md` |
| 16 | C6 | Perform UI, Physiology page, presets v1 | C5 | todo | | | `work_packages/WP-C6_perform_ui_presets.md` |
| 17 | E1 | Fitting framework, losses, identifiability benchmark | D2, B6, C2, C4 | todo | | | `work_packages/WP-E1_fitting_framework.md` |
| 18 | E2 | Voice atlas + Viterbi initial path | E1, C2 | todo | | | `work_packages/WP-E2_voice_atlas_viterbi.md` |
| 19 | E3 | Windowed CMA-ES refinement → gesture scores | E2 | todo | | | `work_packages/WP-E3_windowed_refinement.md` |
| 20 | E4 | Per-bird constants + model adequacy report | E3 | todo | | | `work_packages/WP-E4_bird_constants_adequacy.md` |
| 21 | F1 | Gesture extraction, alignment, clustering | E4 | todo | | | `work_packages/WP-F1_gesture_extraction.md` |
| 22 | F2 | Variation model (FPCA), grammar, voice bank export | F1 | todo | | | `work_packages/WP-F2_variation_model_grammar.md` |
| 23 | F3 | Voice bank runtime integration | F2, C6 | todo | | | `work_packages/WP-F3_voicebank_runtime.md` |
| 24 | F4 | Advanced Gesture page | F3 | todo | | | `work_packages/WP-F4_gesture_page.md` |
| 25 | G1 | Release polish: presets, validation, manual, packaging | F4 | todo | | | `work_packages/WP-G1_release_polish.md` |
| 26 | G2 | Tier-2 physical labial source | B6 | optional | | | `work_packages/WP-G2_tier2_source.md` |
| 27 | G3 | Differentiable twin & amortised inference | E3 | optional | | | `work_packages/WP-G3_differentiable_amortized.md` |

## Current capabilities

The repo builds, tests, and renders. The syrinx now has **two sides**: independent tension and gates, shared air pressure, optional weak coupling, and a 4 ms side switcher for jump-without-glide. Left-only with R gated off matches B4 bit-for-bit. Default trachea reflection is still +0.5 (D-012). D1 is parked until after Phase B.

## Baselines

| Metric | Value | Measured in | Date |
|---|---|---|---|
| CPU per voice @48 kHz (Release, Apple Silicon) | – | B6 | |
| Tuning error, tone 0.6, size 1 (median / max cents) | – | C2 | |
| Fitting benchmark (synthetic) median MRSTFT loss | – | E1 | |
| Real zebra finch resynthesis median MRSTFT loss | – | E3/E4 | |

## Known issues

- `clang-format` is not installed; `check.sh` skips the C++ format check (warns). `brew install clang-format` when convenient.
- `uv` is in `tools/uv` (gitignored), not on the default PATH; use the `scripts/` wrappers.
- `Oversampling::decimate` requires `nHost ≤ maxBlockSize` (chunk in the caller).
- B4 item 06 (tension noise vs real alarm) is noisier, not metallic/FM stacked harmonics — see backlog.
- Weak two-side coupling barely locks (Δβ = 0 / 0 / 0.01 at ε = 0.01 / 0.03 / 0.1).
- Shared α cannot span 1.2–6 kHz; listening 09 tops out near 4.6 kHz.

## Open questions for the human

- D1 is parked (`optional`) until after Phase B. Recordings live at `/Users/baptistebohelay/Dev/research/birds synth/data/annotations/` (`22938.wav` + `22938.json`; custom tseg JSON schema 1.2). When D1 starts: bird identity, label meanings (`peep`, `swipe`, `peep_grillon`, …), and whether to copy into `data/raw/zebra_finch/`.
- JUCE licence choice before any public release (see `decisions.md` D-003).

## Session log (append one line per session)

| Date | WP | Agent/session | Event |
|---|---|---|---|
| 2026-09-16 | – | planning | initial documents created |
| 2026-09-16 | A1 | Cursor Grok 4.6 | started WP-A1 (repo, build, render CLI, listening kit) |
| 2026-09-16 | A1 | Cursor Grok 4.6 | WP-A1 awaiting approval (listening page opened) |
| 2026-09-16 | A1 | Cursor Grok 4.6 | feedback: test.waveform instead of D-013 generator field |
| 2026-09-16 | A1 | Cursor Grok 4.6 | approved, committed and tagged wp-A1 |
| 2026-09-16 | A2 | Cursor Grok 4.6 | started WP-A2 (registry, oversampling, Python bindings) |
| 2026-09-16 | A2 | Cursor Grok 4.6 | WP-A2 awaiting approval (listening page built) |
| 2026-09-16 | A2 | Cursor Grok 4.6 | approved, committed and tagged wp-A2 |
| 2026-09-16 | D1 | Cursor Grok 4.6 | started WP-D1; blocked — `data/raw/zebra_finch/` missing |
| 2026-09-16 | D1 | Cursor Grok 4.6 | human deferred D1 (do B-phase first); recordings path noted; D1 → optional |
| 2026-09-16 | B1 | Cursor Grok 4.6 | started WP-B1 (labial oscillator, parameter guessing, no real audio) |
| 2026-09-16 | B1 | Cursor Grok 4.6 | WP-B1 awaiting approval (listening page opened) |
| 2026-09-16 | B1 | Cursor Grok 4.6 | feedback: t=0 click on silent starts; DC blocker primed; page rebuilt |
| 2026-09-16 | B1 | Cursor Grok 4.6 | approved, committed and tagged wp-B1 |
| 2026-09-16 | B2 | Cursor Grok 4.6 | started WP-B2 (trachea waveguide) |
| 2026-09-16 | B2 | Cursor Grok 4.6 | WP-B2 awaiting approval (listening page opened) |
| 2026-09-16 | B2 | Cursor Grok 4.6 | feedback: trachea too subtle on 02–06; re-rendered stronger examples |
| 2026-09-17 | B2 | Cursor Grok 4.6 | approved, committed and tagged wp-B2 |
| 2026-09-17 | B3 | Cursor Grok 4.6 | started WP-B3 (upper vocal tract: OEC, beak, radiation) |
| 2026-09-17 | B3 | Cursor Grok 4.6 | WP-B3 awaiting approval (M1 listening page opened) |
| 2026-09-17 | B3 | Cursor Grok 4.6 | feedback: sine-like demos, 07/08 motion wrong; listening rebuilt |
| 2026-09-17 | B3 | Cursor Grok 4.6 | 08 metallic/FM noted for B4; rest accepted; awaiting approve |
| 2026-09-17 | B3 | Cursor Grok 4.6 | approved, committed and tagged wp-B3 |
| 2026-09-17 | B4 | Cursor Grok 4.6 | started WP-B4 (respiration & noise) |
| 2026-09-17 | B4 | Cursor Grok 4.6 | WP-B4 awaiting approval (listening page opened) |
| 2026-09-17 | B4 | Cursor Grok 4.6 | feedback: 04–07 listening retuned (pulse, wow, 18 Hz FM, drop M1) |
| 2026-09-17 | B4 | Cursor Grok 4.6 | approved, committed and tagged wp-B4 |
| 2026-09-17 | B5 | Cursor Grok 4.6 | started WP-B5 (two sources, gating, side switching) |
| 2026-09-17 | B5 | Cursor Grok 4.6 | WP-B5 awaiting approval (listening page opened) |
| 2026-09-17 | B5 | Cursor Grok 4.6 | approved, committed and tagged wp-B5 |
