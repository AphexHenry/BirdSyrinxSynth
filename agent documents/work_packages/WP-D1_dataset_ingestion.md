# WP-D1 — Zebra finch dataset ingestion & catalogue

| Field | Value |
|---|---|
| Phase | D — Zebra finch data |
| Depends on | A1 |
| Size | M |
| Milestone | – |

## 1. Goal

Turn the human's annotated zebra finch recordings into a clean, documented, machine-readable dataset: a **manifest** of files and annotated segments, quality checks, non-destructive preprocessing into per-segment clips, and a **catalogue page** the human can browse and listen to. Also write down *observations* about the sounds that will guide the DSP work (noise, harmonic richness, two-voice passages).

## 2. Read first

- `01_research.md` §2.7 (zebra finch song), §8.3 (pipeline overview), §10 (risks: recording quality)
- `02_architecture.md` §3 (data folders), §9.1, §9.6, §11, §12 (data policy)

## 3. Inputs

- Recordings and annotation files copied by the human into `data/raw/zebra_finch/` (any sub-folder structure).
- **If the folder is empty or missing: stop and ask the human** (protocol §8). Ask also for: bird identities (if not obvious from names), what the labels mean (e.g. "i" = introductory note), and whether some files must be excluded.

## 4. Deliverables

| Path | Content |
|---|---|
| `python/cuicui/data/__init__.py`, `discover.py`, `annotations.py`, `quality.py`, `clips.py`, `catalogue.py`, `__main__.py` | ingestion package |
| `schemas/dataset_manifest.schema.json` | schema of the manifest |
| `data/manifest/zf_v1.json` | manifest (committed) |
| `data/manifest/zf_v1_summary.md` | human-readable summary tables (committed) |
| `data/manifest/zf_v1_exclusions.json` | excluded files/segments with reasons (committed) |
| `data/interim/zf_v1/clips/*.wav` + `*.json` sidecars | clips (NOT committed) |
| `python/tests/test_data_*.py` | tests with small synthetic fixtures |

CLI:
```bash
uv run python -m cuicui.data ingest   --raw data/raw/zebra_finch --name zf_v1
uv run python -m cuicui.data clips    --name zf_v1 [--highpass 300] [--pad-ms 20]
uv run python -m cuicui.data catalogue --name zf_v1   # writes listening/D1/ + summary
```

## 5. Specification

### 5.1 Discovery

- Audio: WAV, FLAC, AIFF (via `soundfile`). Record: path (relative to `data/raw/`), sample rate, channels, duration, bit depth, SHA-1 (to detect duplicates).
- Multi-channel files: keep channel index in the manifest; use channel 0 unless the human says otherwise.

### 5.2 Annotation adapters (implement only the formats actually present)

Common interface: `load_annotations(path, audio_info) -> list[Segment(file, t0_s, t1_s, label, source_format, extra)]`.

Formats commonly used for zebra finch song (detect by extension/content):

| Format | Typical file | Notes |
|---|---|---|
| Praat TextGrid | `*.TextGrid` | interval tiers; ask which tier if several |
| Audacity labels | `*.txt` (tab-separated t0, t1, label) | |
| Raven selection table | `*.selections.txt` / `*.txt` with header "Begin Time (s)" | |
| evsonganaly | `*.not.mat` | MATLAB struct: `onsets`, `offsets` (ms), `labels` (string); read with `scipy.io.loadmat` |
| CSV / JSON (e.g. from vak/TweetyNet-like tools) | `*.csv`, `*.json` | columns like onset_s/offset_s/label; confirm with the human |
| Other | – | show 2 examples to the human and ask |

Validation per segment: `0 ≤ t0 < t1 ≤ duration`; overlaps within the same file reported; empty labels reported.

### 5.3 Manifest

`data/manifest/zf_v1.json` (validate with the schema):

```json
{
  "format": "cuicui.dataset_manifest", "version": 1,
  "name": "zf_v1", "created_at": "…", "git_hash": "…",
  "species": "Taeniopygia guttata",
  "recordings": [
    { "id": "rec_0001", "path": "zebra_finch/bird01/2019-05-01_a.wav", "bird": "bird01", "session": "2019-05-01",
      "sample_rate": 44100, "channels": 1, "duration_s": 12.3, "sha1": "…",
      "quality": { "clipping_pct": 0.0, "dc_offset": 0.0001, "noise_floor_db": -62.0, "snr_db": 31.5 } }
  ],
  "segments": [
    { "id": "seg_000001", "recording": "rec_0001", "t0_s": 1.234, "t1_s": 1.301, "label": "a",
      "kind": "syllable", "annotation_file": "zebra_finch/bird01/2019-05-01_a.TextGrid" }
  ],
  "label_map": { "i": "introductory note", "a": "syllable a" }
}
```

`kind` ∈ {`intro`, `syllable`, `call`, `noise`, `unknown`} — derived from the label map confirmed by the human.

### 5.4 Quality metrics (per recording)

- Clipping: % samples with |x| ≥ 0.999 full scale.
- DC offset: mean.
- Noise floor: 10th percentile of frame RMS (20 ms frames) over non-annotated regions, dB FS.
- SNR estimate: median RMS of annotated segments minus noise floor, dB.
- Flag files with clipping > 0.1 %, SNR < 15 dB, or sample rate < 32 kHz.

### 5.5 Clips (non-destructive preprocessing)

- For each segment: extract `[t0 − pad, t1 + pad]` (pad = 20 ms, clamped to file bounds) from the original file.
- Optional high-pass (default 300 Hz, 4th-order Butterworth, zero-phase `sosfiltfilt`) to remove cage/handling noise — record the setting in the sidecar.
- Keep the original sample rate. Write float32 WAV.
- Sidecar JSON: segment id, source file, original t0/t1, pad actually applied, filter settings, peak level.
- Do **not** normalise level (level is information); the catalogue page can normalise its own copies.

### 5.6 Catalogue

- `listening/D1/manifest.json` + page: one group per label (up to 5 examples: shortest, median, longest, 2 random with fixed seed), one group per bird with 2 complete motifs (consecutive segments, from first segment to last of a bout), and a group "quality flags" with the worst files.
- Summary markdown: counts per label and bird, duration statistics (min/median/max), total annotated time, quality table, exclusions.

### 5.7 Observations (in the report)

Look at the spectrograms (and listen) and write a structured section "Observations for the DSP and fitting work":
- note types present per label (harmonic stack, high note, noisy/buzzy, sweep, intro) — a table label → type(s),
- rough f0 range per label (read from spectrograms or a simple autocorrelation estimate; D2 does it properly),
- evidence of **two-voice** passages (two non-harmonically related frequency tracks at the same time),
- evidence of **nonlinear phenomena** (subharmonics, abrupt frequency jumps, chaos-like noise),
- recording problems (reverberation, background birds, clipping, low SNR).

## 6. Suggested steps

1. Inspect `data/raw/zebra_finch/` (file tree, a few annotation files). Ask the human the questions from §3 if needed.
2. Discovery + adapters for the formats found + tests with synthetic fixtures.
3. Manifest + schema + quality metrics.
4. Clips.
5. Catalogue page + summary + observations.

## 7. Out of scope

Feature extraction and proper f0 tracking (D2). Any change to the raw files. Denoising beyond the optional high-pass (can be proposed in backlog).

## 8. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Every audio file under `data/raw/zebra_finch/` is in the manifest or in the exclusions file with a reason. |
| 2 | Every annotation segment is in the manifest or in the exclusions file with a reason (parse error, out of bounds, empty label…). |
| 3 | Each implemented adapter has a unit test on a synthetic fixture (created in the test, not real data). |
| 4 | Clip boundaries: for a synthetic file with clicks at known times, extracted clips start/end within ±1 sample of `t0 − pad` / `t1 + pad`. |
| 5 | Manifest validates against `schemas/dataset_manifest.schema.json`. |
| 6 | Summary markdown lists counts per label and per bird, duration stats, quality flags. |
| 7 | Catalogue page has ≥ 3 examples per label (or all if fewer) and 2 motifs per bird. |
| 8 | Report contains the "Observations" section (§5.7) with a label → note-type table. |
| 9 | `git diff --cached --stat` shows no files from `data/raw/` or `data/interim/`. |
| 10 | `scripts/check.sh` passes. |

## 9. Listening & results

`listening/D1/index.html` (the catalogue, §5.6). In the final message, ask the human to confirm or correct the label → note-type table, because it steers B4/B5 and E-phase priorities.

## 10. Pitfalls

- evsonganaly `.not.mat` times are in **milliseconds**.
- TextGrid files can be UTF-16 encoded.
- Labels may be case-sensitive; do not merge "A" and "a" without asking.
- Very long recordings: stream-read with `soundfile` blocks instead of loading everything.

## Notes from WP-B1 (2026-09-16)

Human deferred D1 until after Phase B (intuitive parameter guessing, no real audio). When this WP starts:

- Recordings: `/Users/baptistebohelay/Dev/research/birds synth/data/annotations/` (`22938.wav` + `22938.json`).
- Annotation format actually present: **custom tseg JSON** (schema 1.2), not TextGrid/Raven/evsonganaly. Implement that adapter. Segmenter code: `/Users/baptistebohelay/Dev/research/birds synth/src/tseg`.
- Status was set to `optional` so protocol does not auto-select D1 before B. Human turns it back to `todo` when they want it.
