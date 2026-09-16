# WP-F4 — Advanced Gesture page

| Field | Value |
|---|---|
| Phase | F — Voice bank & variation |
| Depends on | F3 |
| Size | M |
| Milestone | – |

## 1. Goal

Give **advanced users** (hidden by default) a page to look inside a voice bank and shape it: browse gesture types, see their mean curves and variability, audition them, explore variation modes, draw custom gestures, edit the syllable grammar, and save a user bank. Normal users never need it.

## 2. Read first

- `01_research.md` §7.2 (layers), §7.7–§7.8
- `02_architecture.md` §9.5
- Reports of WP-F2, WP-F3, WP-C6 (UI conventions)

## 3. Deliverables

| Path | Content |
|---|---|
| `plugin/Source/ui/GesturePage.*` | the page (behind an "Advanced" toggle in Settings) |
| `plugin/Source/ui/CurveView.*` | pitch/amp/tone/side curve display with mean ± 2σ bands |
| `plugin/Source/ui/CurveEditor.*` | spline-handle drawing of custom gestures |
| `plugin/Source/ui/GrammarView.*` | transition matrix heat map + editable probabilities |
| `plugin/Source/BankEditing.*` | copy-on-write user bank, edit operations, validation, undo/redo (`juce::UndoManager`) |
| `plugin/Source/PreviewVoice.*` | audition voice (separate from played voices, mixed at −6 dB) |
| `plugin/tests/test_bank_editing.cpp` | tests |

## 4. Specification

### 4.1 Page layout

- Left: list of gesture types (name, count, median duration, movement score); syllable types below.
- Centre: `CurveView` for the selected type: four stacked lanes (pitch, amplitude, tone, side) showing mean curve, ±2σ band (from FPCA variances), and up to 5 sampled examples (button "resample").
- Right: FPCA mode sliders z₁…z_K (in σ units, −2.5…2.5) updating the displayed curve live; "Audition" button plays the displayed gesture at the last played note (or C6 if none) via `PreviewVoice`.
- Bottom: `GrammarView` (states × states heat map; click a cell to edit probability; rows renormalise automatically; gap distributions editable as median/spread).
- Toolbar: "New custom gesture" (opens `CurveEditor`), "Save user bank as…", "Revert to factory", undo/redo.

### 4.2 Editing rules

- Factory banks are read-only: the first edit creates a user copy (copy-on-write) named `<bank> (edited)`.
- Custom gestures: drawn with 4–12 spline handles per lane; saved as a new gesture type with K = 0 (no variation) or with a user "wobble" amount creating a single synthetic variation mode (smooth random curve scaled by wobble).
- Custom gesture types can be added to a syllable type's gesture sequences (with a probability).
- Every edit is validated (schema rules) before being applied to the running engine (swap mechanism of F3).
- Saved to the user banks folder as `bank.json` (schema-valid).

### 4.3 Threading

All editing on the message thread; the engine receives a new immutable bank through F3's swap; preview audio uses a dedicated voice slot in the processor fed by a lock-free command queue.

## 5. Suggested steps

1. Read-only browsing: lists, CurveView, mode sliders, audition.
2. Grammar view (read-only) → editing with renormalisation and undo.
3. Custom gesture editor; save/load user banks.
4. Tests, screenshots, short demo renders.

## 6. Out of scope

Editing physiology tracks of fits; training new banks inside the plugin (Python pipeline only).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Edit → save → reload user bank → identical bank JSON (except timestamps) and schema-valid. |
| 2 | Grammar edits keep all rows normalised (±1e-9); undo/redo restores exact previous matrices. |
| 3 | Custom gesture saved and used by the engine: in a test build, forcing a syllable containing it produces its curve (pitch within 15 cents of the drawn curve on playback). |
| 4 | Audition works while the host plays (no dropouts; zero allocations on the audio thread). |
| 5 | Page hidden unless "Advanced" is enabled; state persists. |
| 6 | pluginval 5 with editor passes; `scripts/check.sh` passes. |

## 8. Listening & results

- Screenshots of the page in the report.
- `listening/F4/`: a mean gesture and its ±2σ variants along mode 1 (rendered through the preview path); a short phrase using a custom drawn gesture inserted into a zebra finch syllable.

Human checklist: open the Advanced page, audition 5 gesture types, move mode sliders, draw one custom gesture, insert it and hear it in a performance.

## 9. Pitfalls

- Don't let the UI mutate the bank object used by the audio thread; always build a new immutable copy.
- Keep the page fast with large banks (hundreds of types): virtualised lists, curve caching.
