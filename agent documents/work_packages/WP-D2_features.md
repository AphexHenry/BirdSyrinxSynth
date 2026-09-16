# WP-D2 — Feature extraction (shared by real recordings and synthetic renders)

| Field | Value |
|---|---|
| Phase | D — Zebra finch data |
| Depends on | D1, B3 |
| Size | M |
| Milestone | – |

## 1. Goal

Build one feature-extraction library that describes a sound frame by frame (1 ms hop): pitch, voicing, harmonic amplitudes, spectral envelope, noisiness and Sound-Analysis-Pro-style features. The **same code** runs on real zebra finch clips and on synthetic renders, so the fitting (E-phase) compares like with like. Validate it on synthetic renders where the true pitch is known, then run it on the whole dataset and summarise each syllable type.

## 2. Read first

- `01_research.md` §2.7, **§8.3–§8.4 (Stage 1)**, §9
- `02_architecture.md` §5.2, §9.6
- Reports of WP-D1 (observations, label → note-type table) and WP-B3

## 3. Deliverables

| Path | Content |
|---|---|
| `python/cuicui/features/__init__.py`, `framing.py`, `pitch.py`, `spectral.py`, `sap.py`, `twovoice.py`, `extract.py`, `__main__.py` | library + CLI |
| `python/cuicui/features/README.md` | every output column: name, unit, definition, frame alignment |
| `python/cuicui/features/synthetic_bench.py` | validation renders with ground truth |
| `data/features/zf_v1/*.parquet` | features for all clips (NOT committed) |
| `data/manifest/zf_v1_features_summary.md` | per-label statistics (committed) |
| `python/tests/test_features_*.py` | tests |

CLI:
```bash
uv run python -m cuicui.features bench            # synthetic validation → listening/D2/bench/
uv run python -m cuicui.features extract --name zf_v1 [--jobs 8]
uv run python -m cuicui.features summary --name zf_v1   # → listening/D2/ + summary md
```

## 4. Specification

### 4.1 Framing

- Hop 1 ms; frame centre times $t_n = n \cdot 1$ ms from clip start.
- Analysis window 10 ms Hann (zero-padded FFT to ≥ 4096 bins at 44.1–48 kHz) for pitch/harmonics; 16 ms multitaper (DPSS, NW = 1.5, K = 2) for SAP features (configurable).
- Work at the clip's native sample rate; all frequencies in Hz.

### 4.2 Pitch (f₀) and voicing

- **Primary:** subharmonic summation (SHS, Hermes 1988) on the log-magnitude spectrum: candidate f₀ grid 300–4000 Hz (configurable), 5-cent steps; salience $S(f) = \sum_{k=1}^{K} h^{k-1}\,|X(kf)|_{interp}$ with $h$ = 0.84, $K$ = up to the Nyquist or 15 harmonics.
- **Secondary:** YIN (de Cheveigné & Kawahara 2002) on the time signal, threshold 0.15.
- **Temporal smoothing:** Viterbi over the top 5 candidates per frame (cost: −log salience + λ·|Δ cents|/100, with λ reduced when salience is very high, so true jumps survive).
- **Voicing probability** ∈ [0,1] from normalised salience and YIN aperiodicity; voiced if > 0.5 and RMS above noise floor + 6 dB.
- Output: `f0_hz` (NaN if unvoiced), `f0_salience`, `voicing_prob`, `voiced`.

### 4.3 Spectral features

- `rms_db` (dBFS), `centroid_hz`, `sci` (= centroid / f₀, NaN if unvoiced).
- `h1_db … h16_db`: harmonic amplitudes (peak within ±3 % of k·f₀, parabolic interpolation), dB relative to the strongest harmonic of the frame; NaN if unvoiced or above Nyquist.
- `env_00 … env_31`: cepstrally smoothed spectral envelope (20 cepstral coefficients) sampled at 32 log-spaced frequencies 300 Hz – 12 kHz, dB relative to frame max.
- `wiener_entropy`: $\log\big(\text{geometric mean} / \text{arithmetic mean}\big)$ of the multitaper power spectrum 300 Hz – 12 kHz (≤ 0; values near 0 = noise-like, very negative = tonal).

### 4.4 SAP-style features (Tchernichovski et al. 2000)

- `goodness_of_pitch`: peak of the cepstrum in the quefrency range of 300–4000 Hz.
- `am`: time derivative of total log power (dB/ms).
- `fm_cents_per_s`: |d f₀/dt| from the smoothed f₀ track (voiced frames).
- (Optional, if time permits: SAP spectral-derivative FM estimate `fm_sap_deg`.)

### 4.5 Two-voice detector

- Per frame: among SHS peaks, find two candidates with salience ratio > 0.5 whose frequency ratio is not within ±3 % of a small-integer ratio (1/2, 1, 2, 3, 3/2, 4/3); flag `two_voice_candidate`; keep `f0b_hz`.
- Segment-level flag if ≥ 10 consecutive frames.

### 4.6 Synthetic validation bench (ground truth)

Using `cuicui_core` (full voice, seeds fixed, tension noise off unless stated):
- **Ground truth f₀** from the oscillator itself: render with `source_only` (same α/β trajectories, same internal rate) and measure instantaneous period by interpolated zero crossings of the DC-removed x (this is exact for our purposes).
- Sets: (a) tonal glides (β sweeps), (b) rich low notes near threshold (f₀ 400–1000 Hz) through the tract with OEC tuned to a high harmonic (upper harmonics louder than f₀ — the zebra finch case), (c) two-voice notes, (d) silences/onsets (known from pressure), (e) (a)+(b) with white noise at 30 and 20 dB SNR, (f) buzzy notes (σ_β = 0.03).

### 4.7 Dataset run and summary

- Extract features for every clip in `zf_v1` (parallel, resumable).
- Per label: median f₀ contour (time-normalised), distributions of f₀, SCI, Wiener entropy, goodness of pitch, duration; fraction of voiced frames; fraction of two-voice frames.
- Heuristic note-type suggestion per label (stack: f₀ < 1000 Hz and h1 not the strongest harmonic; high note: f₀ ≥ 1500 Hz; noisy: median entropy > −2 (tune); sweep: median |FM| large) — compare with the D1 human-confirmed table and report disagreements.

## 5. Suggested steps

1. Framing, spectral features, tests on analytic signals (sines, harmonic complexes, white noise).
2. SHS + YIN + Viterbi pitch; tests.
3. Synthetic bench and tuning of pitch parameters until acceptance criteria pass.
4. SAP features, two-voice detector.
5. Dataset run, summary page.

## 6. Out of scope

Fitting (E-phase), learning-based pitch trackers (could be backlog), denoising.

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Analytic signals: 440 Hz sine → f₀ within 2 cents; harmonic complex with **missing fundamental** (f₀ = 600 Hz, harmonics 2–8) → f₀ = 600 Hz (not 1200); white noise → voiced < 5 % of frames, median Wiener entropy > −0.8 (theory: ≈ −0.58 for a single periodogram, ≈ −0.27 for the K = 2 multitaper spectrum — state which spectrum you use). |
| 2 | Synthetic bench (a) tonal: median absolute error < 5 cents; gross errors (> 50 cents) < 1 % of voiced frames. |
| 3 | Bench (b) rich low notes with strong upper harmonics: median absolute error < 10 cents; gross errors < 3 % (octave errors counted as gross). |
| 4 | Bench (d): voicing F1 ≥ 0.95 against the known sounding frames. |
| 5 | Bench (e) at 20 dB SNR: gross errors < 10 % on (a)+(b). |
| 6 | Bench (c) two-voice: segment-level detection recall ≥ 80 %; false-positive rate on (a)+(b) segments ≤ 5 %. |
| 7 | Frame alignment: a click at a known time appears in `am` at the correct frame (±1 ms). |
| 8 | Dataset run completes for all clips (failures listed with reasons, < 1 %); runtime reported. |
| 9 | Column documentation complete in `features/README.md` (test checks every parquet column is documented). |
| 10 | `scripts/check.sh` passes. |

## 8. Listening & results

- `listening/D2/bench/`: for each synthetic set, the audio + spectrogram with the estimated f₀ track overlaid (and ground truth in another colour), plus an error histogram figure.
- `listening/D2/`: per label, 3 example clips with f₀ track + harmonic amplitude heatmap + entropy plot overlaid on the spectrogram; summary figures (distributions per label).
- Report: bench metrics table, per-label summary table, heuristic vs human note-type comparison.

Final message: ask the human to glance at the f₀ overlays of real syllables and flag any label where the pitch track looks wrong (this matters for fitting).

## 9. Pitfalls

- Zebra finch harmonic stacks often have a weak or missing fundamental → SHS is essential; YIN alone will jump octaves.
- Keep frame times consistent between features computed at different window lengths (centre alignment).
- Use float64 in analysis to avoid precision issues in log spectra.
