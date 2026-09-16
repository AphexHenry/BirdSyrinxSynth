# WP-D1 report — Zebra finch dataset ingestion & catalogue

| Field | Value |
|---|---|
| WP | WP-D1 |
| Status | parked (optional) — human deferred until after Phase B |
| Started | 2026-09-16 |
| Finished | |
| Agent | Cursor Grok 4.6 |
| Sessions | 1 |

## 1. Summary (plain language, 3–6 lines)

Parked. The human asked to guess synthesizer parameters (Phase B) before ingesting recordings. Recordings exist outside this repo (see §12). Next D1 chat should copy/symlink them into `data/raw/zebra_finch/` and implement a **tseg JSON** adapter (not TextGrid / Raven).

## 2. What was built

| File / module | Purpose |
|---|---|
| (none yet) | blocked on raw data |

## 3. How to run it

Not applicable yet.

## 4. Acceptance criteria

| # | Criterion (from the WP) | Result | Evidence (numbers, test name, file) |
|---|---|---|---|
| 1 | Every audio file under `data/raw/zebra_finch/` is in the manifest or exclusions | NOT DONE | waiting for raw data |
| 2 | Every annotation segment is in the manifest or exclusions | NOT DONE | waiting for raw data |
| 3 | Each implemented adapter has a unit test on a synthetic fixture | NOT DONE | |
| 4 | Clip boundaries ±1 sample of t0−pad / t1+pad | NOT DONE | |
| 5 | Manifest validates against schema | NOT DONE | |
| 6 | Summary markdown: counts, duration stats, quality flags | NOT DONE | |
| 7 | Catalogue: ≥ 3 examples per label (or all) and 2 motifs per bird | NOT DONE | |
| 8 | Report "Observations" with label → note-type table | NOT DONE | |
| 9 | Staged files exclude `data/raw/` and `data/interim/` | NOT DONE | |
| 10 | `scripts/check.sh` passes | NOT DONE | |

## 5. Quality gate

Not run yet. `scripts/doctor.sh`: DOCTOR OK (clang-format still missing, warn only).

## 6. Listening & results

Not built yet.

## 7. Deviations from the spec

None yet.

## 8. Contract changes (see decisions.md)

None.

## 9. Measurements & baselines

None.

## 10. Known issues & limitations

- Blocked: no files at `data/raw/zebra_finch/`. Also searched `Dev/research/` for wav/flac/aiff/TextGrid/`*.not.mat` — none found.

## 11. Tools / dependencies added

None yet.

## 12. Handoff notes for the next agent

- Human deferred this WP until after Phase B. Status in `state.md` is `optional`.
- Audio + annotations: `/Users/baptistebohelay/Dev/research/birds synth/data/annotations/`
  - `22938.wav` (48 kHz) + `22938.json` (schema_version 1.2)
  - also `22938.mp3`, `44967.mp3` (no json for 44967), `sample_birdsong.json`
- Segmenter source: `/Users/baptistebohelay/Dev/research/birds synth/src/tseg` (`schema.py`, `annotation.py`)
- Format: custom tseg JSON — `segments[]` with `category` in {SILENCE, NOISE, BIRD_SOUND}, `start`/`end` seconds, optional `elements[].type_label` (`swipe`, `peep`, `peep_grillon`, …) and `fm_level`.
- Still ask: bird identity, label map, exclusions, copy vs symlink into `data/raw/zebra_finch/`.
- Do not commit `data/raw/` or `data/interim/` (D-011).

## 13. Partial — remaining work (only if status is partial)

Waiting for human, then full WP from suggested step 1.

## 14. Revision history (feedback rounds)

| Date | Human feedback | Changes |
|---|---|---|

## 15. Proposed commit message

(not ready)
