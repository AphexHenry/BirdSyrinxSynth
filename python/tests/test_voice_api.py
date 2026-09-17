from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = ROOT / "core" / "tests" / "golden"


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


def _import_core():
    try:
        import cuicui_core as cc
    except ImportError as exc:  # pragma: no cover
        pytest.skip(f"cuicui_core is not built: {exc}")
    return cc


def _spec_to_settings_controls(spec: dict) -> tuple[dict, dict[str, np.ndarray], int, int, int]:
    sr = int(spec["sample_rate"])
    n = int(round(float(spec["duration_s"]) * sr))
    t = np.arange(n, dtype=np.float64) / sr
    settings: dict = {}
    controls: dict[str, np.ndarray] = {}
    params = dict(spec.get("params") or {})
    auto = dict(spec.get("automation") or {})
    ids = set(params) | set(auto)

    def value_at(pid: str, tt: np.ndarray) -> np.ndarray:
        if pid in auto:
            pts = np.asarray(auto[pid], dtype=np.float64)
            return np.interp(tt, pts[:, 0], pts[:, 1]).astype(np.float32)
        if pid in params:
            return np.full(tt.shape, float(params[pid]), dtype=np.float32)
        return np.zeros(tt.shape, dtype=np.float32)

    slow = {
        "body.size",
        "src.gamma",
        "trachea.length_mm",
        "trachea.reflection",
        "trachea.loss",
        "trachea.input_gain",
        "oec.damping",
        "beak.damping",
        "out.radiation",
        "out.gain_db",
        "resp.attack_ms",
        "resp.release_ms",
        "noise.beta_bandwidth_hz",
        "noise.aspiration",
        "src.coupling",
    }
    for pid in ids:
        if pid.startswith("test."):
            continue
        arr = value_at(pid, t)
        if pid in slow and pid not in auto:
            settings[pid] = float(arr[0])
        else:
            controls[pid] = arr
            if pid in slow:
                settings[pid] = float(arr[0])
    seed = int(spec.get("seed", 1))
    os_tok = spec.get("oversampling", "auto")
    oversampling = 0 if os_tok in ("auto", 0, "0") else int(os_tok)
    return settings, controls, sr, seed, oversampling


GOLDEN_SPECS = [
    "b6_tonal_left.render.json",
    "b6_buzzy_noise.render.json",
    "b6_two_voice.render.json",
    "b6_side_switch.render.json",
    "b6_size_0_5.render.json",
    "b6_size_3.render.json",
    "b6_syllable_train.render.json",
]


@pytest.mark.parametrize("name", GOLDEN_SPECS)
def test_render_controls_matches_cli(name: str, tmp_path: Path) -> None:
    cc = _import_core()
    exe = _bird_render()
    spec = json.loads((GOLDEN / name).read_text(encoding="utf-8"))
    wav_path = tmp_path / "a.wav"
    spec_path = tmp_path / "s.render.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    subprocess.run([str(exe), str(spec_path), "-o", str(wav_path)], check=True)
    import soundfile as sf

    cli, sr = sf.read(wav_path, dtype="float32")
    settings, controls, sample_rate, seed, os_ = _spec_to_settings_controls(spec)
    assert sr == sample_rate
    py, _state = cc.render_controls(
        settings, controls, float(sample_rate), float(sample_rate), seed, os_
    )
    py = np.asarray(py, dtype=np.float32)
    assert cli.shape == py.shape
    maxdiff = float(np.max(np.abs(cli.astype(np.float64) - py.astype(np.float64))))
    # CLI primes with JSON doubles (e.g. 0.2); Python/C++ controls are float32 (1 ULP).
    assert maxdiff <= np.finfo(np.float32).eps, f"mismatch for {name} maxdiff={maxdiff}"


def test_voice_save_load_second_half() -> None:
    cc = _import_core()
    n = 48000
    settings = {
        "body.size": 1.0,
        "src.gamma": 24000.0,
        "noise.aspiration": 0.02,
        "noise.beta_bandwidth_hz": 300.0,
    }
    p = np.full(n * 2, 0.54545, dtype=np.float32)
    b = np.full(n * 2, 0.2, dtype=np.float32)
    controls = {
        "resp.pressure": p,
        "src.L.beta": b,
        "src.L.gate": np.ones(n * 2, dtype=np.float32),
        "src.R.gate": np.zeros(n * 2, dtype=np.float32),
        "noise.beta_sigma": np.full(n * 2, 0.01, dtype=np.float32),
    }
    audio, state_mid = cc.render_controls(
        settings,
        {k: v[:n] for k, v in controls.items()},
        48000.0,
        48000.0,
        1,
        0,
    )
    rest, _ = cc.render_controls(
        settings,
        {k: v[n:] for k, v in controls.items()},
        48000.0,
        48000.0,
        1,
        0,
        state_mid,
    )
    full, _ = cc.render_controls(settings, controls, 48000.0, 48000.0, 1, 0)
    full = np.asarray(full, dtype=np.float32)
    rest = np.asarray(rest, dtype=np.float32)
    audio = np.asarray(audio, dtype=np.float32)
    assert audio.shape[0] == n
    assert np.array_equal(full[n:], rest)

    v = cc.Voice(48000.0)
    v.set_settings(settings)
    v.reset(1)
    v.prime(-0.1, 0.2)
    frame = {
        "resp.pressure": 0.54545,
        "src.L.beta": 0.2,
        "src.L.gate": 1.0,
        "src.R.gate": 0.0,
        "noise.beta_sigma": 0.01,
    }
    v.process(frame, frame, 64)
    blob = v.save_state()
    v2 = cc.Voice(48000.0)
    v2.set_settings(settings)
    v2.reset(9)
    v2.load_state(blob)
    a = np.asarray(v.process(frame, frame, 128), dtype=np.float32)
    b2 = np.asarray(v2.process(frame, frame, 128), dtype=np.float32)
    assert np.array_equal(a, b2)
    tel = v.telemetry()
    assert "f0_estimate_hz" in tel
    assert v.latency_samples() >= 0
