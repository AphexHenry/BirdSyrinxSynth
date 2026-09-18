"""Write committed example gesture scores for WP-C4."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
OUT = ROOT / "examples" / "scores"
OUT.mkdir(parents=True, exist_ok=True)

from cuicui.gestures.score_tools import physio_score, sequence  # noqa: E402


def _lerp_breakpoints(points: list[list[float]], t: float) -> float:
    if not points:
        return 0.0
    if t <= points[0][0]:
        return points[0][1]
    if t >= points[-1][0]:
        return points[-1][1]
    for i in range(1, len(points)):
        t0, v0 = points[i - 1]
        t1, v1 = points[i]
        if t <= t1:
            if t1 <= t0:
                return v1
            u = (t - t0) / (t1 - t0)
            return v0 + u * (v1 - v0)
    return points[-1][1]


def _zf_like_physio() -> dict:
    spec = json.loads((ROOT / "examples" / "phrases" / "zf_like_motif.render.json").read_text())
    cr = 1000.0
    dur = float(spec["duration_s"])
    n = int(round(dur * cr))
    auto = spec["automation"]
    params = spec["params"]
    tracks = {}
    for key, pts in auto.items():
        tracks[key] = [_lerp_breakpoints(pts, i / cr) for i in range(n)]
    for key, val in params.items():
        if key in tracks:
            continue
        if key in {
            "src.L.gate",
            "src.R.gate",
            "src.L.beta",
            "src.R.beta",
            "resp.pressure",
            "oec.freq_hz",
            "beak.gape",
            "noise.beta_sigma",
            "src.L.alpha_offset",
            "src.R.alpha_offset",
        }:
            tracks[key] = [float(val)] * n
    settings = {
        "body.size": float(params.get("body.size", 1.0)),
        "src.gamma": float(params.get("src.gamma", 24000.0)),
        "trachea.length_mm": float(params.get("trachea.length_mm", 33.0)),
        "noise.aspiration": float(params.get("noise.aspiration", 0.0)),
        "out.gain_db": float(params.get("out.gain_db", 0.0)),
    }
    return physio_score(tracks, control_rate_hz=cr, settings=settings, name="zf_like_motif")


def main() -> None:
    os.chdir(ROOT)
    song = sequence(
        [
            ("intro", {"dur": 70.0, "depth": 280.0}, 1760.0),
            ("intro", {"dur": 65.0, "depth": 320.0}, 1865.0),
            ("intro", {"dur": 80.0, "depth": 260.0}, 1976.0),
            # Off the D6 noise floor; tone above the tension-noise boost.
            ("stack", {"dur": 200.0, "droop": -25.0, "tone": 0.28, "oec_ratio": 4.5}, 1568.0),
            ("downsweep", {"dur": 180.0, "depth": 800.0, "curvature": 1.6}, 2489.0),
            ("trill", {"dur": 420.0, "rate": 18.0, "element_depth": 280.0, "tone": 0.75}, 2093.0),
            # Rattle (18 Hz AM/FM), not a noise burst.
            ("buzz", {"dur": 220.0, "wobble": 18.0, "tone": 0.12}, 1865.0),
            # Short syllable, closer interval, not a held organ dyad.
            ("two_voice", {"dur": 130.0, "interval": 220.0, "tone": 0.45}, 2489.0),
            ("chip", {"dur": 18.0, "depth": 700.0}, 2093.0),
            ("chip", {"dur": 22.0, "depth": 900.0}, 1976.0),
        ],
        gaps=[0.07, 0.07, 0.14, 0.14, 0.12, 0.14, 0.14, 0.10, 0.06, 0.12],
        name="song_from_archetypes",
        settings={"body.size": 1.0, "perf.breath": 0.72},
        seed=1,
    )
    (OUT / "song_from_archetypes.gscore.json").write_text(json.dumps(song, indent=2) + "\n")

    song2 = json.loads(json.dumps(song))
    song2["name"] = "song_from_archetypes_size2"
    song2["settings"]["body.size"] = 2.0
    (OUT / "song_size2.gscore.json").write_text(json.dumps(song2, indent=2) + "\n")

    gallery = sequence(
        [
            ("whistle", None, 2093.0),
            ("upsweep", None, 2093.0),
            ("downsweep", None, 2093.0),
            ("arch", None, 2093.0),
            ("trill", None, 2093.0),
            ("buzz", None, 1760.0),
            ("stack", None, 1568.0),
            ("chip", None, 2093.0),
            ("two_voice", None, 1760.0),
            ("switch_sweep", None, 4186.0),
            ("intro", None, 1760.0),
        ],
        gap_s=0.08,
        name="gallery_defaults",
        seed=1,
    )
    (OUT / "gallery_defaults.gscore.json").write_text(json.dumps(gallery, indent=2) + "\n")

    phys = _zf_like_physio()
    (OUT / "zf_like_motif.gscore.json").write_text(json.dumps(phys, indent=2) + "\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
