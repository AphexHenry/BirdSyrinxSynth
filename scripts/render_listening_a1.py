"""Render WP-A1 listening examples (run from repo root via uv)."""

from __future__ import annotations

import hashlib
import json
import math
import subprocess
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "build" / "release" / "bin" / "bird_render"
OUT = ROOT / "listening" / "A1"


def _render(spec: dict, wav: Path, extra: list[str] | None = None) -> None:
    spec_path = wav.with_suffix(".render.json")
    spec_path.write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
    cmd = [str(RENDER), str(spec_path), "-o", str(wav)]
    if extra:
        cmd.extend(extra)
    subprocess.run(cmd, check=True)


def _log_sweep_points(t0: float, t1: float, f0: float, f1: float, n: int = 64) -> list[list[float]]:
    points: list[list[float]] = []
    for i in range(n):
        u = i / (n - 1)
        t = t0 + u * (t1 - t0)
        f = f0 * (f1 / f0) ** u
        points.append([round(t, 6), round(f, 4)])
    return points


def main() -> None:
    if not RENDER.is_file():
        raise SystemExit(f"missing {RENDER}; run scripts/build.sh release")
    OUT.mkdir(parents=True, exist_ok=True)

    sweep = {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": 4.0,
        "seed": 1,
        "params": {"test.gain_db": -12.0},
        "automation": {"test.freq_hz": _log_sweep_points(0.0, 4.0, 100.0, 12000.0)},
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }
    _render(sweep, OUT / "01_sweep_log_100_12k.wav")

    ramp = {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": 2.0,
        "seed": 1,
        "params": {"test.freq_hz": 1000.0},
        "automation": {"test.gain_db": [[0.0, -40.0], [1.0, 0.0], [2.0, -40.0]]},
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }
    _render(ramp, OUT / "02_tone_1k_gain_ramp.wav")

    noise_spec = {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": 1.0,
        "params": {"test.gain_db": -12.0, "test.waveform": "noise"},
        "output": {"normalize_peak_dbfs": None},
    }
    n1 = OUT / "_noise_seed1.wav"
    n2 = OUT / "_noise_seed2.wav"
    _render(noise_spec, n1, extra=["--seed", "1"])
    _render(noise_spec, n2, extra=["--seed", "2"])
    a, sr = sf.read(n1)
    b, sr_b = sf.read(n2)
    assert sr == sr_b
    sf.write(OUT / "03_two_seeds_noise.wav", np.concatenate([a, b]), sr, subtype="FLOAT")
    n1.unlink()
    n2.unlink()
    (OUT / "_noise_seed1.render.json").unlink(missing_ok=True)
    (OUT / "_noise_seed2.render.json").unlink(missing_ok=True)

    # Determinism evidence
    dup = OUT / "_det_b.wav"
    _render(ramp, dup)
    sha_a = hashlib.sha256((OUT / "02_tone_1k_gain_ramp.wav").read_bytes()).hexdigest()
    sha_b = hashlib.sha256(dup.read_bytes()).hexdigest()
    dup.unlink()
    (OUT / "_det_b.render.json").unlink(missing_ok=True)
    (OUT / "determinism.json").write_text(
        json.dumps({"file": "02_tone_1k_gain_ramp.wav", "sha256": sha_a, "match": sha_a == sha_b}, indent=2)
        + "\n",
        encoding="utf-8",
    )
    if sha_a != sha_b:
        raise SystemExit("determinism check failed")

    # Metrics for the manifest
    def dur(path: Path) -> float:
        info = sf.info(str(path))
        return info.frames / info.samplerate

    def peak_dbfs(path: Path) -> float:
        x, _sr = sf.read(path)
        peak = float(np.max(np.abs(x)))
        return 20.0 * math.log10(peak) if peak > 0 else -120.0

    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "A1",
        "title": "Repository, render CLI, listening kit — test tones",
        "items": [
            {
                "file": "01_sweep_log_100_12k.wav",
                "title": "Log sine sweep 100 Hz → 12 kHz",
                "listen_for": "Clean 4 s sweep, no clicks or extra tones. Spectrogram: one thin rising line.",
                "params": {"test.gain_db": -12.0, "f0": "100→12000 Hz log"},
                "metrics": {
                    "duration_s": round(dur(OUT / "01_sweep_log_100_12k.wav"), 4),
                    "peak_dbfs": round(peak_dbfs(OUT / "01_sweep_log_100_12k.wav"), 2),
                },
                "group": None,
                "reference": None,
            },
            {
                "file": "02_tone_1k_gain_ramp.wav",
                "title": "1 kHz, gain −40 → 0 → −40 dB",
                "listen_for": "Smooth level change, no clicks at the start, peak, or end.",
                "params": {"test.freq_hz": 1000.0, "test.gain_db": "-40→0→-40"},
                "metrics": {
                    "duration_s": round(dur(OUT / "02_tone_1k_gain_ramp.wav"), 4),
                    "peak_dbfs": round(peak_dbfs(OUT / "02_tone_1k_gain_ramp.wav"), 2),
                    "sha256": sha_a,
                },
                "group": None,
                "reference": None,
            },
            {
                "file": "03_two_seeds_noise.wav",
                "title": "White noise, two seeds",
                "listen_for": "Two different 1 s noise bursts (seed 1, then seed 2). They must not sound identical.",
                "params": {"test.waveform": "noise", "seeds": [1, 2]},
                "metrics": {
                    "duration_s": round(dur(OUT / "03_two_seeds_noise.wav"), 4),
                    "peak_dbfs": round(peak_dbfs(OUT / "03_two_seeds_noise.wav"), 2),
                },
                "group": None,
                "reference": None,
            },
        ],
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote listening files in {OUT}")
    print(f"determinism sha256={sha_a}")


if __name__ == "__main__":
    main()
