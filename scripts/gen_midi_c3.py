#!/usr/bin/env python3
"""Write WP-C3 MIDI phrases into examples/midi/ (SMF format 0)."""

from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "examples" / "midi"
TPB = 480  # ticks per beat
BPM = 120.0


def _vlq(n: int) -> bytes:
    n = max(0, n)
    buf = [n & 0x7F]
    n >>= 7
    while n:
        buf.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(buf))


def write_smf(path: Path, events: list[tuple[int, bytes]]) -> None:
    events = sorted(events, key=lambda e: (e[0], e[1][0] if e[1] else 0))
    body = bytearray()
    last = 0
    for tick, payload in events:
        body.extend(_vlq(tick - last))
        body.extend(payload)
        last = tick
    body.extend(_vlq(0))
    body.extend(b"\xff\x2f\x00")
    track = struct.pack(">4sI", b"MTrk", len(body)) + bytes(body)
    header = struct.pack(">4sIHHH", b"MThd", 6, 0, 1, TPB)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + track)


def ticks(seconds: float) -> int:
    return int(round(seconds * TPB * BPM / 60.0))


def note_on(ch: int, note: int, vel: int) -> bytes:
    return bytes([0x90 | (ch & 0x0F), note & 0x7F, vel & 0x7F])


def note_off(ch: int, note: int, vel: int = 0) -> bytes:
    return bytes([0x80 | (ch & 0x0F), note & 0x7F, vel & 0x7F])


def cc(ch: int, num: int, val: int) -> bytes:
    return bytes([0xB0 | (ch & 0x0F), num & 0x7F, val & 0x7F])


def aftertouch(ch: int, val: int) -> bytes:
    return bytes([0xD0 | (ch & 0x0F), val & 0x7F])


def pitch_bend(ch: int, value_14: int) -> bytes:
    value_14 = max(0, min(16383, value_14))
    return bytes([0xE0 | (ch & 0x0F), value_14 & 0x7F, (value_14 >> 7) & 0x7F])


def held_notes(pairs: list[tuple[int, float, float, int]]) -> list[tuple[int, bytes]]:
    """(note, start_s, dur_s, vel) — overlapping starts are legato."""
    ev: list[tuple[int, bytes]] = []
    for note, start, dur, vel in pairs:
        ev.append((ticks(start), note_on(0, note, vel)))
        ev.append((ticks(start + dur), note_off(0, note)))
    return ev


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)

    # Chromatic D6–C8 (playable band at σ=1).
    chrom = [(n, 0.28 * (n - 86), 0.24, 100) for n in range(86, 109)]
    write_smf(OUT / "chromatic.mid", held_notes(chrom))

    # C major over the playable range: E6 … C8 (C-major pitches ≥ D6).
    cmaj = [88, 89, 91, 93, 95, 96, 98, 100, 101, 103, 105, 107, 108]
    scale = [(n, 0.32 * i, 0.28, 100) for i, n in enumerate(cmaj)]
    write_smf(OUT / "scale.mid", held_notes(scale))

    # Short melody (C7-centred).
    melody_notes = [96, 98, 100, 103, 101, 98, 96, 93, 95, 96]
    melody = [(n, 0.28 * i, 0.24, 100) for i, n in enumerate(melody_notes)]
    write_smf(OUT / "melody.mid", held_notes(melody))

    # Legato phrase (overlap).
    legato = [
        (96, 0.00, 0.55, 100),
        (98, 0.40, 0.50, 100),
        (100, 0.80, 0.50, 95),
        (103, 1.20, 0.70, 100),
        (96, 1.75, 0.55, 90),
    ]
    write_smf(OUT / "legato.mid", held_notes(legato))

    # Octave / tenth jumps, legato (glide 0 in listening).
    # Stay off D6 (MIDI 86): that is the SNIC/onset floor and sounds like a printer.
    # A6=93 is well inside the periodic band; A7=105 octave; C8=108 minor tenth.
    jumps = [
        (93, 0.00, 0.55, 100),
        (105, 0.48, 0.55, 100),
        (93, 1.05, 0.50, 100),
        (108, 1.50, 0.60, 100),
        (93, 2.10, 0.55, 100),
    ]
    write_smf(OUT / "jumps.mid", held_notes(jumps))

    # CC74 tone sweep on a held C7.
    t_end = ticks(3.0)
    sweep: list[tuple[int, bytes]] = [
        (0, note_on(0, 96, 100)),
        (t_end, note_off(0, 96)),
    ]
    for i in range(0, 128):
        sweep.append((ticks(0.15 + 2.5 * i / 127.0), cc(0, 74, i)))
    write_smf(OUT / "tone_cc74.mid", sweep)

    # Aftertouch swells on C7.
    at: list[tuple[int, bytes]] = [
        (0, note_on(0, 96, 100)),
        (ticks(2.4), note_off(0, 96)),
    ]
    for i, v in enumerate([0, 20, 50, 90, 40, 110, 30, 80, 0]):
        at.append((ticks(0.2 + 0.22 * i), aftertouch(0, v)))
    write_smf(OUT / "aftertouch.mid", at)

    print(f"wrote MIDI into {OUT}")


if __name__ == "__main__":
    main()
