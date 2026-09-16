# WP-F1 — Gesture extraction, alignment, clustering

| Field | Value |
|---|---|
| Phase | F — Voice bank & variation |
| Depends on | E4 |
| Size | M |
| Milestone | – |

## 1. Goal

Convert the fitted physiological trajectories of real zebra finch syllables into **musical-layer gestures** (pitch, amplitude, tone, side + hints), cut them at natural boundaries, align them in time and group them into **gesture types**. Verify that playing these gestures back through the pitch-led controller still sounds like the real syllables (round trip). This is the "find a series of likely gesture curves" step.

## 2. Read first

- `01_research.md` **§7.7–§7.8**, §8.3 (Stage 6), §8.4 (round-trip requirement)
- `02_architecture.md` §9.4
- Reports of WP-E4 (model adequacy: which labels are reliable), WP-C4 (gesture structures, score player), WP-C3 (controller)

## 3. Deliverables

| Path | Content |
|---|---|
| `python/cuicui/gestures/convert.py` | physio tracks → musical-layer curves |
| `python/cuicui/gestures/segment.py` | gesture boundaries |
| `python/cuicui/gestures/align.py` | time normalisation, DTW, DBA templates |
| `python/cuicui/gestures/cluster.py` | k-medoids clustering, model selection |
| `python/cuicui/gestures/roundtrip.py` | gestures → score → render → losses |
| `python/cuicui/gestures/__main__.py` | CLI |
| `data/gestures/zf_v1/instances.parquet`, `clusters.json` | results (instances NOT committed; `clusters.json` committed if < 1 MB) |
| `python/tests/test_gestures_*.py` | tests |

CLI:
```bash
uv run python -m cuicui.gestures extract --fits data/fits/zf_final --out data/gestures/zf_v1
uv run python -m cuicui.gestures cluster --in data/gestures/zf_v1
uv run python -m cuicui.gestures roundtrip --in data/gestures/zf_v1 --jobs 8
```

## 4. Specification

### 4.1 Conversion (per syllable, 1 ms)

- **Pitch:** $f_0(t) = \nu(\alpha(t), \beta(t))\,\gamma/2\pi$ from the C2 forward atlas (noise-free, exact for the model) → cents (absolute).
- **Amplitude:** RMS dB of the *resynthesised* audio (D2 `rms_db`), relative to the syllable peak.
- **Tone:** $(\alpha - \alpha_{lo}(\nu))/(\alpha_{hi}(\nu) - \alpha_{lo}(\nu))$ clamped [0, 1].
- **Side:** from gates: $g_R - g_L$ (single-side fits: −1).
- **Hints (time series, summarised per gesture later):** oec_ratio = f_H/f₀, gape; second_voice_cents for two-source fits.
- Silent frames (p below threshold or amplitude < −40 dB) are marked.

### 4.2 Segmentation into gestures

Boundaries at:
1. silences ≥ 5 ms (always),
2. side changes,
3. **extrema of the smoothed pitch curve** (Savitzky–Golay, 7 ms) with prominence ≥ 50 cents (inspired by gesture trajectory extrema, Amador et al. 2013),
4. amplitude minima deeper than 6 dB between two maxima.
Constraints: minimum gesture duration 10 ms (merge with the neighbour that yields the smallest pitch discontinuity), maximum 250 ms (split at the largest curvature point).

### 4.3 Registration and distance

- Resample each gesture to N = 64 points on u ∈ [0,1]; pitch relative to the anchor = median pitch of the central 50 %; amplitude relative to the gesture peak.
- Distance: multichannel DTW with Sakoe–Chiba band 10 %: $d = \mathrm{DTW}(w_P\,\Delta P/100, w_A\,\Delta A/6, w_T\,\Delta T/0.2, w_S\,\Delta S)$ + $w_D\,|\log(\text{dur}_1/\text{dur}_2)|$; default weights 1, 0.5, 0.5, 1, 0.5.

### 4.4 Clustering

- k-medoids (PAM) on the DTW distance matrix; k from 6 to 40; choose k maximising mean silhouette subject to every cluster having ≥ 5 members (small clusters merged into their nearest medoid); report the silhouette curve.
- Per cluster: medoid, **DBA template** (DTW barycentre averaging, 10 iterations), members aligned to the template (warping paths stored) → registered curves ready for FPCA (F2).
- Per cluster statistics: count, duration distribution, anchor offsets relative to the syllable anchor, hints medians, movement score (RMS of dP/du), labels it occurs in.
- Exclude labels that E4 judged unreliable (list them in the report).

### 4.5 Round trip

For every syllable: build a gesture score with the extracted gesture events (anchors in Hz, durations, curves, hints) and the bird's settings, render with the C4 player (λ = 1 path), compute E1 losses against (a) the real recording and (b) the E3/E4 physiological resynthesis.
**Requirement:** median over syllables of loss(a) ≤ 1.2 × median loss of the physiological resynthesis vs real.

## 5. Suggested steps

1. Conversion + tests (synthetic fits where curves are known from archetype scores → convert → compare to the original archetype curves).
2. Segmentation + tests.
3. Round trip on a few syllables early (catch conversion problems before clustering).
4. DTW, clustering, DBA; galleries.
5. Full round trip; report.

## 6. Out of scope

FPCA, grammar, voice bank export (F2); runtime (F3).

## 7. Tests & acceptance criteria

| # | Criterion |
|---|---|
| 1 | Conversion on synthetic archetype scores (C4 → physio tracks recorded → convert): pitch curve error ≤ 10 cents median, tone error ≤ 0.05 median, side exact. |
| 2 | Segmentation on synthetic concatenations of known archetypes: ≥ 90 % of true boundaries found within ±5 ms; ≤ 10 % spurious. |
| 3 | DTW implementation matches a reference implementation (e.g. `tslearn` or a simple O(n²) Python DTW in tests) on random curves within 1e-9. |
| 4 | Clustering report: k chosen, silhouette curve, cluster sizes, per-cluster templates figure. |
| 5 | Round trip requirement (§4.5) met, or the report explains which conversions lose information and proposes fixes (then human decides). |
| 6 | `scripts/check.sh` passes. |

## 8. Listening & results

- `listening/F1/clusters/`: per cluster: medoid + 4 random members rendered from gestures (short silence between), template curves figure, list of labels.
- `listening/F1/roundtrip/`: per label 3 syllables: real vs physiological resynthesis vs gesture-level resynthesis (group).
- Report: cluster table (id, count, median duration, movement score, typical labels, a descriptive name you give it such as "fast downsweep", "flat stack").

Ask the human to listen to the cluster galleries and suggest names or merges for clusters that sound the same.

## 9. Pitfalls

- Pitch from the atlas is exact only where the fit's (α, β) are in periodic regimes; flag irregular frames and use D2 f₀ of the resynthesis there.
- DTW distance matrices grow as O(n²) in memory: for many thousands of gestures, compute in chunks with float32 and consider clustering per label first, then merging.
- Keep anchor definitions identical between extraction (here) and generation (F2/F3).
