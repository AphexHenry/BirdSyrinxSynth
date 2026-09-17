"""Render WP-C1 listening examples through plugin_render."""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
from pathlib import Path

os.environ.setdefault(
    "MPLCONFIGDIR",
    str(Path(__file__).resolve().parents[1] / "listening" / "C1" / ".mplconfig"),
)
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "build" / "release" / "bin" / "plugin_render"
OUT = ROOT / "listening" / "C1"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "C1"


def _run(args: list[str]) -> None:
    subprocess.run(args, check=True)


def _peak_norm_stereo(y: np.ndarray, peak_db: float = -3.0) -> np.ndarray:
    peak = float(np.max(np.abs(y))) if y.size else 0.0
    if peak <= 1e-12:
        return y
    g = (10.0 ** (peak_db / 20.0)) / peak
    return (y * g).astype(np.float32)


def _write_midi(path: Path, events: list[tuple[int, bytes]]) -> None:
    """Minimal SMF format 0. events: (tick, midi_payload_without_status_running)."""
    events = sorted(events, key=lambda e: e[0])
    tick_rate = 480
    body = bytearray()
    last = 0
    for tick, payload in events:
        dt = tick - last
        last = tick
        # VLQ
        vlq = bytearray()
        n = dt
        vlq.append(n & 0x7F)
        n >>= 7
        while n:
            vlq.append(0x80 | (n & 0x7F))
            n >>= 7
        body.extend(reversed(vlq))
        body.extend(payload)
    # end of track
    body.extend(b"\x00\xff\x2f\x00")
    track = struct.pack(">4sI", b"MTrk", len(body)) + bytes(body)
    header = struct.pack(">4sIHHH", b"MThd", 6, 0, 1, tick_rate)
    path.write_bytes(header + track)


def _ticks(seconds: float, tpb: int = 480, bpm: float = 120.0) -> int:
    return int(round(seconds * tpb * bpm / 60.0))


def main() -> int:
    if not RENDER.is_file():
        print(f"missing {RENDER}; run scripts/build.sh release", file=sys.stderr)
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)

    # 01: 8 notes, legato + detached
    notes_01 = ",".join(
        [
            "67:0.00:0.35:100",
            "69:0.28:0.30:95",
            "72:0.70:0.18:90",
            "67:1.00:0.18:90",
            "64:1.30:0.40:110",
            "60:1.60:0.28:100",
            "62:2.05:0.16:80",
            "60:2.30:0.35:100",
        ]
    )
    wav01 = OUT / "01_midi_phrase_crude.wav"
    _run(
        [
            str(RENDER),
            "--notes",
            notes_01,
            "--sr",
            "48000",
            "--block",
            "256",
            "--tail",
            "0.6",
            "--seed",
            "1",
            "-o",
            str(wav01),
        ]
    )

    # 02: same note, four velocities (floor 55: vel 30 sits in a noisy/onset regime)
    notes_02 = "60:0.0:0.45:55,60:0.7:0.45:75,60:1.4:0.45:100,60:2.1:0.45:127"
    wav02 = OUT / "02_velocity_layers.wav"
    _run(
        [
            str(RENDER),
            "--notes",
            notes_02,
            "--sr",
            "48000",
            "--block",
            "256",
            "--tail",
            "0.5",
            "--seed",
            "1",
            "-o",
            str(wav02),
        ]
    )

    # 03: sustained note + OEC sweep
    auto = {
        "oec.freq_hz": [[0.0, 800.0], [1.2, 6000.0], [2.4, 800.0]],
    }
    auto_path = OUT / "03_oec_automation.json"
    auto_path.write_text(json.dumps(auto), encoding="utf-8")
    wav03 = OUT / "03_oec_automation.wav"
    _run(
        [
            str(RENDER),
            "--notes",
            "72:0.0:2.5:100",
            "--automation",
            str(auto_path),
            "--sr",
            "48000",
            "--block",
            "256",
            "--tail",
            "0.4",
            "--seed",
            "1",
            "-o",
            str(wav03),
        ]
    )

    # 04: pitch bend up then down
    tpb = 480
    bend_events: list[tuple[int, bytes]] = [
        # MIDI 60 (C4): equal-tempered crude map, ±2 st is a clear glide
        (_ticks(0.0), bytes([0x90, 60, 100])),
    ]
    # 14-bit pitch bend, center 8192, +2 st = 16383, -2 st = 0
    def pb(tick: int, value: int) -> None:
        value = max(0, min(16383, value))
        lsb = value & 0x7F
        msb = (value >> 7) & 0x7F
        bend_events.append((tick, bytes([0xE0, lsb, msb])))

    pb(_ticks(0.0), 8192)
    pb(_ticks(0.4), 16383)
    pb(_ticks(0.9), 8192)
    pb(_ticks(1.3), 0)
    pb(_ticks(1.8), 8192)
    bend_events.append((_ticks(2.1), bytes([0x80, 60, 0])))
    mid = OUT / "04_bend.mid"
    _write_midi(mid, bend_events)
    wav04 = OUT / "04_bend.wav"
    _run(
        [
            str(RENDER),
            "--midi",
            str(mid),
            "--sr",
            "48000",
            "--block",
            "256",
            "--tail",
            "0.5",
            "--seed",
            "1",
            "-o",
            str(wav04),
        ]
    )

    items = []
    for wav, title, listen, extra in [
        (
            wav01,
            "MIDI phrase (crude tuning)",
            "Notes start and stop like breath. Pitch should step with the keys — C and D different, C4 to C5 about an octave. Still a bird-range map, not piano tuning.",
            {"notes": 8},
        ),
        (
            wav02,
            "Velocity layers",
            "Four hits of the same key at velocities 55, 75, 100, 127. Later hits should be louder and a bit brighter, with a sharper start. (Velocity 30 was a noisy printer-like onset, not a quiet bird.)",
            {"velocities": [55, 75, 100, 127]},
        ),
        (
            wav03,
            "OEC automation",
            "Held note while the mouth-cavity frequency sweeps 800 → 6000 → 800 Hz. The colour should move smoothly, without zipper steps.",
            {"oec_hz": [800, 6000, 800]},
        ),
        (
            wav04,
            "Pitch bend",
            "C4 (MIDI 60): bend up two semitones, back, down two, then centre. The glide should be smooth and clearly audible.",
            {"bend_st": 2, "midi_note": 60},
        ),
    ]:
        y, sr = sf.read(str(wav), always_2d=True)
        y = _peak_norm_stereo(y)
        sf.write(str(wav), y, sr, subtype="FLOAT")
        peak = float(np.max(np.abs(y)))
        items.append(
            {
                "file": wav.name,
                "title": title,
                "listen_for": listen,
                "params": extra,
                "metrics": {"peak": round(peak, 4), "duration_s": round(len(y) / sr, 3)},
            }
        )

    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "C1",
        "title": "Plugin shell — MIDI through the bird voice",
        "items": items,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"wrote {OUT / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
