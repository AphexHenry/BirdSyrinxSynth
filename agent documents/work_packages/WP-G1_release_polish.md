# WP-G1 — Release polish: presets, validation, manual, packaging

| Field | Value |
|---|---|
| Phase | G — Release & extensions |
| Depends on | F4 |
| Size | L |
| Milestone | **M7 — release candidate** |

## 1. Goal

Turn the working instrument into a **release candidate**: frozen parameter set, final factory presets, strict validation across formats and DAWs, performance and robustness passes, a musician-facing manual, third-party notices, and a macOS installer ready for signing and notarisation.

## 2. Read first

- `02_architecture.md` §1, §7, §10, §12, §13
- `decisions.md` (D-003 licence and D-004 naming **must be resolved** — if still open, stop and ask the human before packaging)
- All milestone reports (M1–M6) and `backlog.md`

## 3. Deliverables

| Path | Content |
|---|---|
| `params.def` | test parameters removed; version hints final; parameter freeze note in `decisions.md` |
| `assets/presets/` | ≥ 40 factory presets, reviewed, consistent loudness (±2 dB on the standard test) |
| `docs/manual/manual.md` → `docs/manual/Syrinx_Manual.pdf` (and HTML) | musician manual |
| `docs/THIRD_PARTY_NOTICES.md` | licences of JUCE, clap-juce-extensions, nlohmann/json, etc. |
| `CHANGELOG.md`, version `1.0.0-rc1` | |
| `packaging/macos/` | `pkgbuild`/`productbuild` scripts for VST3, AU, CLAP, Standalone; signing/notarisation scripts with placeholders for the human's Apple Developer identity |
| `scripts/release.sh` | build → validate → package |
| `agent documents/reports/WP-G1_report.md` | incl. DAW compatibility matrix and performance tables |

## 4. Specification

### 4.1 Parameter freeze

- Remove `test.*` parameters; verify no automation/preset references them.
- Review every parameter name, range, default, unit and help text for musicians; list changes in the report; after release, parameter IDs and ranges must not change (decision entry).

### 4.2 Validation

- pluginval **strictness 8** (VST3, AU), `auval`, CLAP validator if available.
- Sample rates 44.1, 48, 88.2, 96, 192 kHz; block sizes 16–4096; live sample-rate change; offline bounce equals real-time render (−60 dB difference).
- Soak test: 1 h generative playing with parameter automation and bank switches: no crash, no non-finite resets, memory stable.
- Performance: CPU table (1, 8, 16 voices × 48/96 kHz) vs budgets; optimise hot spots if needed (profile first; SIMD or loop restructuring only with tests proving bit-identical or within −90 dB output).

### 4.3 DAW compatibility (human-assisted)

Matrix for Logic Pro (AU), Ableton Live (VST3/AU), Reaper (VST3/AU/CLAP), Bitwig (CLAP/VST3), Standalone: load, play, automate, save/reload project, offline bounce, preset recall. The agent prepares a checklist file `docs/qa/daw_checklist.md`; the human fills it; the agent summarises it in the report.

### 4.4 Manual (for musicians)

Chapters: What Syrinx is (short, with a picture of the vocal organ chain) · Quick start · The 8 macros · Perform, Physiology, Settings and Advanced pages · Voice banks (archetypes, zebra finch birds) · MIDI and MPE mapping · Playable ranges per Size (table from C2) · Tips (melodies, textures, flocks, tempo sync) · FAQ/troubleshooting · Scientific background (one page, references from `01_research.md` §11) · Credits.

### 4.5 Packaging

- macOS installer (`.pkg`) with selectable components (VST3, AU, CLAP, Standalone) installing to standard locations; uninstall instructions.
- Signing and notarisation scripts use environment variables for identity and credentials — **never commit credentials**; the human runs the final signing step.

## 5. Suggested steps

1. Resolve open decisions with the human (licence, name) — ask immediately at session start if open.
2. Parameter freeze + presets pass.
3. Validation, soak, performance.
4. Manual and notices.
5. Packaging scripts; DAW checklist; report.

## 6. Out of scope

Windows/Linux installers, AUv3, new features (backlog).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | D-003 and D-004 have status `accepted`. |
| 2 | pluginval strictness 8 passes (VST3, AU); `auval` passes. |
| 3 | Sample-rate/block-size matrix and offline-vs-realtime check pass. |
| 4 | 1 h soak test passes (log attached). |
| 5 | CPU budgets from B6/C5 met at 48 kHz; 96 kHz numbers reported. |
| 6 | ≥ 40 presets, loudness spread ≤ ±2 dB on the standard test, every preset has a description. |
| 7 | Manual complete (all chapters), PDF and HTML generated. |
| 8 | Installer builds; installs all components on a clean user account (human confirms). |
| 9 | DAW checklist filled by the human and summarised. |
| 10 | `scripts/check.sh` and `scripts/release.sh` (without signing) pass. |

## 8. Listening & results — milestone M7

- `listening/G1/`: all factory presets with the standard test file; a 2–3 minute "demo reel" MIDI arrangement (created by the agent from simple MIDI parts) showcasing tonal melodies, zebra finch banks, flocks and giant birds.
- Report: validation logs summary, CPU tables, DAW matrix.

## 9. Pitfalls

- Parameter ID or AU/VST3 code changes after this point break users' sessions — double-check before tagging the RC.
- Notarisation requires hardened runtime settings in the JUCE project; test early with an ad-hoc signature.
