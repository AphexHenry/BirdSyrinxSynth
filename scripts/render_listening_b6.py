"""Render WP-B6 listening examples (milestone M2: complete voice)."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", str(Path(__file__).resolve().parents[1] / "listening" / "B6" / ".mplconfig"))
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
RENDER = ROOT / "build" / "release" / "bin" / "bird_render"
BENCH = ROOT / "build" / "release" / "bin" / "bird_bench"
OUT = ROOT / "listening" / "B6"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "B6"
FIGS = OUT / "figs"
PHRASE = ROOT / "examples" / "phrases" / "zf_like_motif.render.json"
TWO = ROOT / "examples" / "phrases" / "two_voice_motif.render.json"
REAL = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")


def _run_render(spec: dict, wav: Path) -> None:
    tmp = wav.with_suffix(".render.json")
    tmp.write_text(json.dumps(spec), encoding="utf-8")
    subprocess.run([str(RENDER), str(tmp), "-o", str(wav)], check=True)


def _peak_norm(y: np.ndarray, peak_db: float = -3.0) -> np.ndarray:
    peak = float(np.max(np.abs(y))) if y.size else 0.0
    if peak <= 1e-12:
        return y
    g = (10.0 ** (peak_db / 20.0)) / peak
    return (y * g).astype(np.float32)


def _concat(parts: list[np.ndarray], sr: int, gap_s: float = 0.12) -> np.ndarray:
    gap = np.zeros(int(round(gap_s * sr)), dtype=np.float32)
    out: list[np.ndarray] = []
    for i, p in enumerate(parts):
        out.append(np.asarray(p, dtype=np.float32))
        if i + 1 < len(parts):
            out.append(gap)
    return np.concatenate(out) if out else np.zeros(0, dtype=np.float32)


def _load_spec(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _zc_f0(x: np.ndarray, sr: float, skip_s: float = 0.05) -> float:
    y = np.asarray(x, dtype=np.float64)
    i0 = int(round(skip_s * sr))
    sl = y[i0:] - np.mean(y[i0:]) if y.size > i0 + 32 else y
    z = np.where((sl[:-1] <= 0.0) & (sl[1:] > 0.0))[0]
    if z.size < 4:
        return 0.0
    t = (i0 + z) / sr
    dt = np.diff(t)
    dt = dt[(dt > 1e-5) & (dt < 0.02)]
    return float(1.0 / np.mean(dt)) if dt.size >= 3 else 0.0


def main() -> None:
    if not RENDER.is_file():
        raise SystemExit("build bird_render first (scripts/build.sh release)")
    OUT.mkdir(parents=True, exist_ok=True)
    FIGS.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)

    sr = 48000
    phrase = _load_spec(PHRASE)
    sizes = (0.5, 1.0, 2.0, 4.0)
    chunks: list[np.ndarray] = []
    f0s: dict[str, float] = {}
    for sigma in sizes:
        spec = json.loads(json.dumps(phrase))
        spec["params"]["body.size"] = sigma
        wav = OUT / f"_size_{sigma}.wav"
        _run_render(spec, wav)
        y, got = sf.read(wav, dtype="float32")
        assert got == sr
        chunks.append(y)
        f0s[f"sigma_{sigma}"] = _zc_f0(y, sr)
    y01 = _peak_norm(_concat(chunks, sr, 0.15))
    sf.write(OUT / "01_size_sweep_phrase.wav", y01, sr)

    sweep = json.loads(json.dumps(phrase))
    sweep["duration_s"] = 4.0
    sweep["params"]["resp.pressure"] = 0.55
    sweep["params"]["src.L.beta"] = 0.25
    sweep["automation"] = {
        "resp.pressure": [[0.0, 0.55], [4.0, 0.55]],
        "src.L.beta": [[0.0, 0.25], [4.0, 0.25]],
        "body.size": [[0.0, 0.25], [2.0, 4.0], [4.0, 1.0]],
    }
    _run_render(sweep, OUT / "02_size_continuous.wav")
    y02, _ = sf.read(OUT / "02_size_continuous.wav", dtype="float32")
    sf.write(OUT / "02_size_continuous.wav", _peak_norm(y02), sr)

    _run_render(phrase, OUT / "03_demo_phrase_zf_like.wav")
    y03, _ = sf.read(OUT / "03_demo_phrase_zf_like.wav", dtype="float32")
    sf.write(OUT / "03_demo_phrase_zf_like.wav", _peak_norm(y03), sr)

    two = _load_spec(TWO)
    _run_render(two, OUT / "04_demo_two_voice_phrase.wav")
    y04, _ = sf.read(OUT / "04_demo_two_voice_phrase.wav", dtype="float32")
    sf.write(OUT / "04_demo_two_voice_phrase.wav", _peak_norm(y04), sr)

    golden = json.loads((ROOT / "core/tests/golden/b6_tonal_left.render.json").read_text(encoding="utf-8"))
    rate_parts: list[np.ndarray] = []
    f0_sr: dict[str, float] = {}
    for rate in (44100, 48000, 96000):
        spec = json.loads(json.dumps(golden))
        spec["sample_rate"] = rate
        wav = OUT / f"_sr_{rate}.wav"
        _run_render(spec, wav)
        y, got = sf.read(wav, dtype="float32")
        f0_sr[str(rate)] = _zc_f0(y, got)
        if got != sr:
            g = np.gcd(got, sr)
            y = resample_poly(y, sr // g, got // g).astype(np.float32)
        rate_parts.append(y)
    y05 = _peak_norm(_concat(rate_parts, sr, 0.12))
    sf.write(OUT / "05_sample_rates.wav", y05, sr)

    # Size-scaling spectra (steady note, last 0.25 s of each size chunk).
    plt.figure(figsize=(7.2, 3.6))
    for sigma, y in zip(sizes, chunks, strict=True):
        sl = y[-int(0.25 * sr) :]
        spec = np.abs(np.fft.rfft(sl * np.hanning(sl.size)))
        freqs = np.fft.rfftfreq(sl.size, 1.0 / sr)
        mag = 20.0 * np.log10(np.maximum(spec, 1e-12))
        mag -= np.max(mag)
        plt.plot(freqs, mag, label=f"σ={sigma}", lw=1.1)
    plt.xlim(0, 8000)
    plt.ylim(-60, 3)
    plt.xlabel("Hz")
    plt.ylabel("dB (peak-normalised)")
    plt.title("Size scaling — same α, β; spectrum compresses with σ")
    plt.legend()
    plt.tight_layout()
    plt.savefig(FIGS / "size_spectra.png", dpi=120)
    plt.close()

    bench_txt = ""
    if BENCH.is_file():
        proc = subprocess.run([str(BENCH)], check=True, capture_output=True, text=True)
        bench_txt = proc.stdout
        (OUT / "bench.txt").write_text(bench_txt, encoding="utf-8")
        fig, ax = plt.subplots(figsize=(8.4, 2.8))
        ax.axis("off")
        ax.set_title("CPU (Release, this Mac, OS auto, block 256)")
        ax.text(0.0, 0.95, bench_txt, family="monospace", fontsize=7, va="top")
        plt.tight_layout()
        plt.savefig(FIGS / "cpu_table.png", dpi=120)
        plt.close()

    shutil.copy2(FIGS / "size_spectra.png", ASSETS / "size_spectra.png")
    if (FIGS / "cpu_table.png").is_file():
        shutil.copy2(FIGS / "cpu_table.png", ASSETS / "cpu_table.png")

    real_file = None
    if REAL.is_file():
        audio, rsr = sf.read(REAL, dtype="float32")
        if audio.ndim > 1:
            audio = audio.mean(axis=1)
        # A real singing bout (consecutive swipes), not a match of the synth phrase.
        i0, i1 = int(15.45 * rsr), int(17.70 * rsr)
        clip = np.asarray(audio[i0:i1], dtype=np.float32)
        if rsr != sr:
            g = np.gcd(rsr, sr)
            clip = resample_poly(clip, sr // g, rsr // g).astype(np.float32)
        sf.write(OUT / "03b_real_zf_motif.wav", _peak_norm(clip), sr)
        real_file = "03b_real_zf_motif.wav"

    items = [
        {
            "file": "01_size_sweep_phrase.wav",
            "title": "Same phrase at σ = 0.5, 1, 2, 4",
            "listen_for": "A bigger bird, not a slowed-down tape: pitch, trachea ‘ring’ and beak brightness all drop together.",
            "params": {"body.size": "0.5 → 1 → 2 → 4"},
            "metrics": {k: round(v, 1) for k, v in f0s.items()},
            "group": None,
            "reference": None,
        },
        {
            "file": "02_size_continuous.wav",
            "title": "Sustained note, σ swept 0.25 → 4 → 1",
            "listen_for": "Smooth morph, no clicks or zipper noise as size moves.",
            "params": {"body.size": "0.25 → 4 → 1"},
            "metrics": {},
            "group": None,
            "reference": None,
        },
        {
            "file": "03_demo_phrase_zf_like.wav",
            "title": "Made-up phrase (not a copy of a recording)",
            "listen_for": (
                "Four invented notes: two short, a longer downsweep, a mid note. "
                "Judge plausibility only — nothing here is fitted to the real bird."
            ),
            "params": {"phrase": "examples/phrases/zf_like_motif.render.json"},
            "metrics": {"f0_hz": round(_zc_f0(y03, sr), 1)},
            "group": "motif_context" if real_file else None,
            "reference": None,
        },
        {
            "file": "04_demo_two_voice_phrase.wav",
            "title": "Two sound sources (left and right labia)",
            "listen_for": (
                "A songbird has two little voices in one throat. "
                "First: left only (lower). Then: right only (higher). "
                "Then: one breath hops left→right — pitch jumps, no glide. "
                "Last: both at the same time, two pitches at once."
            ),
            "params": {"phrase": "examples/phrases/two_voice_motif.render.json"},
            "metrics": {},
            "group": None,
            "reference": None,
        },
        {
            "file": "05_sample_rates.wav",
            "title": "Same note at 44.1 / 48 / 96 kHz (resampled to 48 kHz)",
            "listen_for": "The three concatenations should sound like the same bird, not three tunings.",
            "params": {"sample_rate": "44100, 48000, 96000"},
            "metrics": {f"f0_{k}": round(v, 1) for k, v in f0_sr.items()},
            "group": None,
            "reference": None,
        },
    ]
    if real_file:
        items.insert(
            3,
            {
                "file": real_file,
                "title": "Real zebra finch (different song — not a match)",
                "listen_for": (
                    "Same species, different phrase. Sit beside the made-up synth "
                    "only so you can hear the grain of a real bird. Fitting comes later."
                ),
                "params": {"clip": "22938.wav 15.45–17.70 s"},
                "metrics": {},
                "group": "motif_context",
                "reference": None,
            },
        )
    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "B6",
        "title": "Complete voice — body size, two sides, state (M2)",
        "items": items,
        "figures": [
            {"file": "figs/size_spectra.png", "title": "Size scaling spectra", "caption": "σ=2 should look like σ=1 compressed by 2× on the frequency axis."},
            {"file": "figs/cpu_table.png", "title": "CPU benchmarks", "caption": "One voice is ~2.7 % of a core at 48 kHz (target was 0.5 %). 16 voices RT≈0.43 (target ≤0.1). See report."},
        ],
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
