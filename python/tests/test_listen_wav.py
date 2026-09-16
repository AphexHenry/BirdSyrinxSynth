from __future__ import annotations

import hashlib
import json
import os
import subprocess
from pathlib import Path

import numpy as np
import pytest
import soundfile as sf

ROOT = Path(__file__).resolve().parents[2]


def _bird_render() -> Path:
    env = os.environ.get("BIRD_RENDER")
    if env:
        p = Path(env)
        if p.is_file():
            return p
    for p in (
        ROOT / "build" / "release" / "bin" / "bird_render",
        ROOT / "build" / "debug" / "bin" / "bird_render",
    ):
        if p.is_file():
            return p
    pytest.skip("bird_render is not built (run scripts/build.sh first)")


def _write_spec(
    path: Path, *, duration: float = 0.2, freq: float = 1000.0, gain: float = -6.0
) -> None:
    spec = {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": duration,
        "seed": 1,
        "oversampling": 1,
        "params": {"test.freq_hz": freq, "test.gain_db": gain},
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }
    path.write_text(json.dumps(spec), encoding="utf-8")


def test_soundfile_reads_float32_and_pcm24(tmp_path: Path) -> None:
    exe = _bird_render()
    spec = tmp_path / "tone.render.json"
    _write_spec(spec, duration=0.05, freq=1000.0, gain=-6.0)

    f32 = tmp_path / "f32.wav"
    pcm = tmp_path / "pcm24.wav"
    subprocess.run([str(exe), str(spec), "-o", str(f32), "--format", "float32"], check=True)
    subprocess.run([str(exe), str(spec), "-o", str(pcm), "--format", "pcm24"], check=True)

    a, sr_a = sf.read(f32, dtype="float32")
    b, sr_b = sf.read(pcm, dtype="float32")
    assert sr_a == 48000
    assert sr_b == 48000
    n = 2400
    assert a.shape[0] == n
    assert b.shape[0] == n
    t = np.arange(n, dtype=np.float64) / 48000.0
    expected = (10.0 ** (-6.0 / 20.0) * np.sin(2.0 * np.pi * 1000.0 * t)).astype(np.float32)
    assert float(np.max(np.abs(a.astype(np.float64) - expected.astype(np.float64)))) < 1e-6
    assert float(np.max(np.abs(a.astype(np.float64) - b.astype(np.float64)))) < 1e-6


def test_determinism_sha256(tmp_path: Path) -> None:
    exe = _bird_render()
    spec = tmp_path / "tone.render.json"
    _write_spec(spec, duration=0.1, freq=440.0, gain=-12.0)
    a = tmp_path / "a.wav"
    b = tmp_path / "b.wav"
    subprocess.run([str(exe), str(spec), "-o", str(a)], check=True)
    subprocess.run([str(exe), str(spec), "-o", str(b)], check=True)

    def sha(p: Path) -> str:
        h = hashlib.sha256()
        h.update(p.read_bytes())
        return h.hexdigest()

    assert sha(a) == sha(b)


def test_unknown_param_exits_2(tmp_path: Path) -> None:
    exe = _bird_render()
    spec = tmp_path / "bad.render.json"
    spec.write_text(
        json.dumps(
            {
                "format": "cuicui.render_spec",
                "version": 1,
                "params": {"not.a.param": 1.0},
            }
        ),
        encoding="utf-8",
    )
    out = tmp_path / "out.wav"
    proc = subprocess.run([str(exe), str(spec), "-o", str(out)], capture_output=True, text=True)
    assert proc.returncode == 2
    assert "not.a.param" in proc.stderr
    assert "test.freq_hz" in proc.stderr
    assert "test.waveform" in proc.stderr
