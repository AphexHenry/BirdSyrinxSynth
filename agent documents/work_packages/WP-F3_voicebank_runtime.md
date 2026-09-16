# WP-F3 — Voice bank runtime integration

| Field | Value |
|---|---|
| Phase | F — Voice bank & variation |
| Depends on | F2, C6 |
| Size | L |
| Milestone | **M6 — learned voice bank** |

## 1. Goal

Make the plugin sing with the zebra finch voice banks: load banks safely, implement `VoiceBankSource` in C++ with **exact parity** with the Python reference generator, integrate with all macros (Leash, Motion, Range, Speed, Variation, Density, Snap, Size, tempo sync, freeze, polyphony), add a "replay real song" mode, and ship factory banks and presets.

## 2. Read first

- `01_research.md` §7.5–§7.8
- `02_architecture.md` §9.5, §6
- Reports of WP-C5 (engine, `VoiceBankSource` stub), WP-C6 (UI, presets), WP-F2 (reference generator, PRNG parity)

## 3. Deliverables

| Path | Content |
|---|---|
| `core/include/cuicui/io/voice_bank_io.hpp` | JSON loader → immutable `VoiceBank` (offline thread) |
| `core/include/cuicui/control/voice_bank.hpp` | `VoiceBank` data structures (fixed-size, cache-friendly) |
| `core/include/cuicui/control/voice_bank_source.hpp` | `VoiceBankSource` implementing `GestureSource` |
| `plugin/Source/BankManager.*` | factory banks (BinaryData), user banks folder, safe swap |
| `plugin/Source/ui/PerformPage.*` | bank selector wired, "Replay real song" toggle |
| `python/bindings/module.cpp` | `generate_from_bank(bank_json, settings, seed, seconds)` for parity tests |
| `assets/presets/*.preset.json` | ≥ 6 new presets using zebra finch banks |
| `core/tests/test_voice_bank_source.cpp`, `python/tests/test_bank_parity.py`, `plugin/tests/test_bank_switch.cpp` | tests |

## 4. Specification

### 4.1 Loading and swapping

- Banks are parsed on a background/message thread into an immutable `VoiceBank` (all arrays preallocated).
- Handover to the audio thread through a lock-free pointer swap (e.g. atomic raw pointer + deferred deletion on the message thread after the audio thread acknowledges); **no allocation or deallocation on the audio thread**.
- A bank change applies at the next note-on of each voice; running phrases finish with the old bank.
- `perf.voice_bank` choice list: factory banks known at build time (`archetypes`, `zebra_finch_<bird>` …) + "User bank" (the actual user bank path stored as a non-automatable state property).

### 4.2 VoiceBankSource

- Same state machine as C5's `GestureEngine`, but syllables come from the bank's grammar and syllable types, gesture curves from FPCA sampling (§9.5 rule), durations from log-normal models, gaps from the grammar's distributions — **identical rules and draw order as the Python reference generator (F2 §4.5)**.
- Motion bias, Range, Speed, Density, tempo sync, freeze and motif memory behave as in C5 (reuse code; refactor shared parts into a base class if useful).
- Hints: oec_ratio/gape medians per gesture type applied as in C3 when Leash > 0.5.
- Couplings from the bank (`amp_per_octave_db`, `oec_tracking`) override the host defaults while a bank is active (the Physiology page shows the effective values).
- Bird settings (`settings` in the bank: trachea, dampings, γ…) are applied as the voice's anatomy while the bank is active; user can switch "Use bank anatomy" off (then host values apply).

### 4.3 Replay real song mode

- Toggle on the Perform page: the source replays the bank's example bouts (gesture-level scores of real bouts, stored in `examples/`) anchored on the held note, with Variation forced to 0 and no invention; Speed and Range still apply.

### 4.4 Parity

- `generate_from_bank` (C++ via bindings) and the F2 Python reference, for the same bank, settings, seed and duration, must produce identical event lists (syllable ids, gesture type ids, durations) and gesture curves within 1e-6.

## 5. Suggested steps

1. Loader + data structures + schema checks.
2. `VoiceBankSource` core with parity tests (bindings) before plugin integration.
3. Plugin integration: BankManager, swap, UI.
4. Anatomy/couplings application, replay mode.
5. Presets, renders, human checklist.

## 6. Out of scope

Gesture editing UI (F4), new banks from other species (backlog).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Parity (§4.4) for 3 banks × 5 seeds × Variation {0, 1, 2} × Motion {0.2, 0.8}. |
| 2 | Bank switching stress test: 60 s of playing while switching banks every 500 ms → zero allocations/deallocations on the audio thread, no crashes, no clicks > −40 dBFS beyond normal note behaviour. |
| 3 | Invalid bank file → clear error message in the UI, previous bank stays active. |
| 4 | Stereotypy in the plugin: Variation 0, Leash 1, held note 30 s → generated syllable sequence matches the bank's most frequent motif order ≥ 90 % (via telemetry/event log in a test build). |
| 5 | CPU: 8 voices with bank source within C5's measured budget × 1.2. |
| 6 | pluginval 5 (VST3, AU) passes; `scripts/check.sh` passes. |

## 8. Listening & results — milestone M6

`listening/F3/` (plugin_render):

| File | Content | Listen for |
|---|---|---|
| `01_bank_leash1_var0.wav` vs real bout (reference) | held note, Variation 0 | the bird's own motif, recognisably zebra finch |
| `02_bank_var1.wav`, `03_bank_var2.wav` | Variation 1 and 2 | natural variety; invention |
| `04_bank_motion_sweep.wav` | Motion 0 → 1 | calmer vs busier choice of gestures |
| `05_bank_melody_leash03.wav` | melody with bank "commentary" | musical use |
| `06_bank_flock.wav` | chord, polyphony 6 | zebra finch colony |
| `07_replay_real_song.wav` vs real bout | replay mode | faithful replay at the played pitch |
| `08_bank_size2.wav` | Variation 1 at σ = 2 | giant zebra finch |
| `09_presets_bank_*.wav` | new presets with the standard test file | |

Final message: **milestone M6**. Human checklist: play the zebra finch banks in a DAW; judge realism at Variation 0 and musical usefulness at 1–2; try replay mode.

## 9. Pitfalls

- Draw-order parity is fragile: document the exact order of random draws in both implementations (comment block at the top of both files).
- JSON parsing of large banks must never happen on the audio thread (including when the host restores state: parse on the message thread, then swap).
