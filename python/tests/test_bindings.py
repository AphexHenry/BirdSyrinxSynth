from __future__ import annotations

import json
import os
import subprocess
import threading
import time
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


def _import_core():
    try:
        import cuicui_core as cc
    except ImportError as exc:  # pragma: no cover
        pytest.skip(f"cuicui_core is not built: {exc}")
    return cc


def _norm_specs(items: list[dict]) -> list[dict]:
    out: list[dict] = []
    for raw in items:
        d = dict(raw)
        for key in ("min", "max", "default", "skew", "smoothing_ms"):
            d[key] = float(d[key])
        d["fast"] = bool(d["fast"])
        d["id"] = str(d["id"])
        d["name"] = str(d["name"])
        d["unit"] = str(d["unit"])
        d["layer"] = str(d["layer"])
        d["since"] = str(d["since"])
        out.append(d)
    return sorted(out, key=lambda x: x["id"])


def test_dump_params_matches_bindings() -> None:
    cc = _import_core()
    exe = _bird_render()
    proc = subprocess.run([str(exe), "--dump-params"], check=True, capture_output=True, text=True)
    cli = json.loads(proc.stdout)
    assert _norm_specs(cli) == _norm_specs(list(cc.param_specs()))


def test_resolve_oversampling_auto() -> None:
    cc = _import_core()
    assert cc.resolve_oversampling(44100.0, 0) == 4
    assert cc.resolve_oversampling(48000.0, 0) == 4
    assert cc.resolve_oversampling(88200.0, 0) == 2
    assert cc.resolve_oversampling(96000.0, 0) == 2
    assert cc.resolve_oversampling(176400.0, 0) == 1
    assert cc.resolve_oversampling(192000.0, 0) == 1


def _spec(waveform: str, os: int, duration: float = 0.05) -> dict:
    return {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": duration,
        "seed": 1,
        "oversampling": os,
        "params": {
            "test.waveform": waveform,
            "test.freq_hz": 1000.0 if waveform == "sine" else 3000.0,
            "test.gain_db": -12.0,
        },
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }


def test_render_spec_bit_identical_to_cli(tmp_path: Path) -> None:
    cc = _import_core()
    exe = _bird_render()
    cases = (
        _spec("sine", 4),
        _spec("saw", 1),
        _spec("saw", 4),
    )
    for spec in cases:
        spec_path = tmp_path / "s.render.json"
        wav_path = tmp_path / "a.wav"
        spec_path.write_text(json.dumps(spec), encoding="utf-8")
        subprocess.run([str(exe), str(spec_path), "-o", str(wav_path)], check=True)
        cli, sr = sf.read(wav_path, dtype="float32")
        assert sr == 48000
        py = np.asarray(cc.render_spec(spec), dtype=np.float32)
        assert cli.shape == py.shape
        assert np.array_equal(cli, py), (
            f"mismatch for {spec['params']['test.waveform']} os={spec['oversampling']}"
        )


def test_side_switcher_equal_power() -> None:
    cc = _import_core()
    sw = cc.SideSwitcher()
    sw.prepare(1000.0)
    sw.set_crossfade_ms(4.0)
    sw.set_active(1)
    last_power = None
    for _ in range(4):
        _bl, _br, g_l, g_r = sw.next(0.2, 0.8)
        power = g_l * g_l + g_r * g_r
        assert abs(power - 1.0) < 1e-9
        last_power = (g_l, g_r)
    assert last_power is not None
    assert abs(last_power[0] - 0.0) < 1e-12
    assert abs(last_power[1] - 1.0) < 1e-12


def test_gil_released_two_threads() -> None:
    cc = _import_core()
    spec = _spec("saw", 4, duration=5.0)

    def _work() -> None:
        cc.render_spec(spec)

    t0 = time.perf_counter()
    _work()
    _work()
    sequential = time.perf_counter() - t0

    t0 = time.perf_counter()
    threads = [threading.Thread(target=_work) for _ in range(2)]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    parallel = time.perf_counter() - t0

    speedup = sequential / parallel if parallel > 0 else 0.0
    assert speedup >= 1.5, f"GIL speedup {speedup:.2f}x (seq={sequential:.2f}s par={parallel:.2f}s)"
