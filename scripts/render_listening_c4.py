"""Render WP-C4 listening examples (archetype gallery + scores)."""

from __future__ import annotations

import json
import os
import shutil
import sys
from pathlib import Path

os.environ.setdefault(
    "MPLCONFIGDIR",
    str(Path(__file__).resolve().parents[1] / "listening" / "C4" / ".mplconfig"),
)
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
OUT = ROOT / "listening" / "C4"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "C4"
FIGS = OUT / "figs"
SR = 48000.0

from cuicui.gestures.score_tools import archetype_event  # noqa: E402
from cuicui.listen.spectrogram import save_spectrogram  # noqa: E402

ARCH = [
    "whistle",
    "upsweep",
    "downsweep",
    "arch",
    "trill",
    "buzz",
    "stack",
    "chip",
    "two_voice",
    "switch_sweep",
    "intro",
]

ANCHOR = {
    "whistle": 2093.0,
    "upsweep": 2093.0,
    "downsweep": 2093.0,
    "arch": 2093.0,
    "trill": 2093.0,
    "buzz": 1760.0,
    "stack": 1568.0,
    "chip": 2093.0,
    "two_voice": 1760.0,
    "switch_sweep": 4186.0,
    "intro": 1760.0,
}

LOW = {
    "whistle": {"dur": 120.0, "arch": 20.0},
    "upsweep": {"dur": 70.0, "depth": 400.0},
    "downsweep": {"dur": 70.0, "depth": 400.0},
    "arch": {"dur": 120.0, "depth": 200.0},
    "trill": {"dur": 250.0, "rate": 12.0, "element_depth": 150.0},
    "buzz": {"dur": 120.0, "wobble": 12.0},
    "stack": {"dur": 100.0, "droop": -10.0},
    "chip": {"dur": 12.0, "depth": 300.0},
    "two_voice": {"dur": 140.0, "interval": 200.0},
    "switch_sweep": {"dur": 100.0, "depth": 800.0},
    "intro": {"dur": 40.0, "depth": 150.0},
}

HIGH = {
    "whistle": {"dur": 450.0, "arch": 180.0},
    "upsweep": {"dur": 220.0, "depth": 1600.0},
    "downsweep": {"dur": 220.0, "depth": 1600.0},
    "arch": {"dur": 280.0, "depth": 900.0},
    "trill": {"dur": 700.0, "rate": 32.0, "element_depth": 800.0},
    "buzz": {"dur": 400.0, "wobble": 80.0},
    "stack": {"dur": 280.0, "droop": -80.0, "oec_ratio": 7.0},
    "chip": {"dur": 32.0, "depth": 1400.0},
    "two_voice": {"dur": 320.0, "interval": 700.0},
    "switch_sweep": {"dur": 220.0, "depth": 2200.0},
    "intro": {"dur": 100.0, "depth": 500.0},
}

LISTEN = {
    "whistle": "A pure-ish bird note with a gentle arch. Three lengths/arches.",
    "upsweep": "Rises into the note. Low: small slide; high: wide whoop.",
    "downsweep": "Falls off the note. Low: modest drop; high: steep fall.",
    "arch": "Up then down. High is a bigger hill.",
    "trill": "Repeated little drops. Low: slow and shallow; high: fast and deep.",
    "buzz": "A rattly syllable (about 18 Hz), not a hiss. High has more wobble.",
    "stack": "A darker, overtone-y note (OEC on a high harmonic). Should be a vowel, not noise.",
    "chip": "A tiny call, 10–30 ms. High is deeper and a bit longer.",
    "two_voice": "Two pitches at once with a short envelope. High widens the interval.",
    "switch_sweep": "A long fall that hops from the right side to the left in the middle.",
    "intro": "Short harmonic downsweep — zebra-finch-like introductory notes.",
}


def _cc():
    import cuicui_core as cc

    return cc


def _peak_norm(y: np.ndarray, peak_db: float = -3.0) -> np.ndarray:
    peak = float(np.max(np.abs(y))) if y.size else 0.0
    if peak <= 1e-12:
        return y.astype(np.float32)
    g = (10.0 ** (peak_db / 20.0)) / peak
    return (y * g).astype(np.float32)


def _silence(ms: float) -> np.ndarray:
    n = int(round(ms * 0.001 * SR))
    return np.zeros(max(n, 0), dtype=np.float32)


def _render_events(events: list[dict], size: float = 1.0, breath: float = 0.75) -> np.ndarray:
    cc = _cc()
    score = {
        "format": "cuicui.gesture_score",
        "version": 1,
        "settings": {"body.size": size, "perf.breath": breath},
        "gestures": events,
    }
    return np.asarray(cc.render_score(score, SR), dtype=np.float32)


def _gallery_wav(name: str) -> np.ndarray:
    ahz = ANCHOR[name]
    parts = [
        _render_events([archetype_event(name, 0.01, params=None, anchor_hz=ahz, seed=1)]),
        _silence(300),
        _render_events([archetype_event(name, 0.01, params=LOW[name], anchor_hz=ahz, seed=2)]),
        _silence(300),
        _render_events([archetype_event(name, 0.01, params=HIGH[name], anchor_hz=ahz, seed=3)]),
    ]
    return _peak_norm(np.concatenate(parts))


def _curve_figure(name: str, path: Path) -> None:
    cc = _cc()
    gen = cc.generate_archetype(name, {}, 1)
    u = np.linspace(0.0, 1.0, 64)
    fig, axes = plt.subplots(4, 1, figsize=(7.2, 7.0), sharex=True)
    axes[0].plot(u, gen["curves"]["pitch_cents"], color="#e8a54b")
    axes[0].set_ylabel("pitch (¢)")
    axes[1].plot(u, gen["curves"]["amp_db"], color="#7ec8e3")
    axes[1].set_ylabel("amp (dB)")
    axes[2].plot(u, gen["curves"]["tone"], color="#9ad09a")
    axes[2].set_ylabel("tone")
    axes[2].set_ylim(-0.05, 1.05)
    axes[3].plot(u, gen["curves"]["side"], color="#d08ad0")
    axes[3].set_ylabel("side")
    axes[3].set_ylim(-1.15, 1.15)
    axes[3].set_xlabel("u")
    fig.suptitle(f"{name} (defaults)", fontsize=12)
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    plt.close(fig)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    FIGS.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)

    items = []
    figures = []
    for i, name in enumerate(ARCH, start=1):
        wav = OUT / f"{i:02d}_gallery_{name}.wav"
        y = _gallery_wav(name)
        sf.write(wav, y, int(SR), subtype="FLOAT")
        fig = FIGS / f"curves_{name}.png"
        _curve_figure(name, fig)
        items.append(
            {
                "file": wav.name,
                "title": f"Gallery — {name} (default / low / high)",
                "listen_for": LISTEN[name],
                "params": {"archetype": name, "anchor_hz": ANCHOR[name]},
                "metrics": {"duration_s": round(len(y) / SR, 3)},
                "group": "gallery",
            }
        )
        figures.append(
            {
                "file": f"figs/{fig.name}",
                "title": f"{name} curves",
                "caption": "pitch (cents), amplitude (dB), tone, side vs normalised time u.",
            }
        )

    song_path = ROOT / "examples" / "scores" / "song_from_archetypes.gscore.json"
    song = json.loads(song_path.read_text(encoding="utf-8"))
    y12 = _peak_norm(np.asarray(_cc().render_score(song, SR), dtype=np.float32))
    sf.write(OUT / "12_song_from_archetypes.wav", y12, int(SR), subtype="FLOAT")
    items.append(
        {
            "file": "12_song_from_archetypes.wav",
            "title": "A phrase from archetypes only",
            "listen_for": "Slower phrase: 3 intro notes, a dark stack (not hiss), downsweep, trill, "
            "a rattly buzz, a short two-voice syllable, chip pair. Listen whether it could be a bird.",
            "params": {"score": "examples/scores/song_from_archetypes.gscore.json"},
            "metrics": {"duration_s": round(len(y12) / SR, 3)},
        }
    )

    song2 = json.loads((ROOT / "examples" / "scores" / "song_size2.gscore.json").read_text())
    y13 = _peak_norm(np.asarray(_cc().render_score(song2, SR), dtype=np.float32))
    sf.write(OUT / "13_same_song_size2.wav", y13, int(SR), subtype="FLOAT")
    items.append(
        {
            "file": "13_same_song_size2.wav",
            "title": "Same phrase at size 2",
            "listen_for": "Same contour, bigger/lower bird. The two-voice syllable should still "
            "read as a note, not an organ interval.",
            "params": {"body.size": 2.0},
            "metrics": {"duration_s": round(len(y13) / SR, 3)},
            "group": "size",
            "reference": "12_song_from_archetypes.wav",
        }
    )

    phys = json.loads((ROOT / "examples" / "scores" / "zf_like_motif.gscore.json").read_text())
    y14 = _peak_norm(np.asarray(_cc().render_score(phys, SR), dtype=np.float32))
    sf.write(OUT / "14_physio_score.wav", y14, int(SR), subtype="FLOAT")
    items.append(
        {
            "file": "14_physio_score.wav",
            "title": "B6 demo phrase as a physio-track score",
            "listen_for": "Same four-syllable phrase as B6 listening 03: should match that render.",
            "params": {"score": "examples/scores/zf_like_motif.gscore.json"},
            "metrics": {"duration_s": round(len(y14) / SR, 3)},
        }
    )

    # Song spectrogram as a report figure
    save_spectrogram(OUT / "12_song_from_archetypes.wav", FIGS / "song_spec.png")

    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "C4",
        "title": "Archetypes and gesture scores",
        "items": items,
        "figures": figures[:11]
        + [
            {
                "file": "figs/song_spec.png",
                "title": "Song from archetypes",
                "caption": "Spectrogram of 12_song_from_archetypes.wav.",
            }
        ],
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    # Copy a few figures into the report assets (≤ 10).
    copied = [
        FIGS / "curves_whistle.png",
        FIGS / "curves_upsweep.png",
        FIGS / "curves_trill.png",
        FIGS / "curves_buzz.png",
        FIGS / "curves_stack.png",
        FIGS / "curves_chip.png",
        FIGS / "curves_two_voice.png",
        FIGS / "curves_switch_sweep.png",
        FIGS / "curves_intro.png",
    ]
    for src in copied:
        if src.is_file():
            shutil.copyfile(src, ASSETS / src.name)

    print(f"wrote {len(items)} items → {OUT}")


if __name__ == "__main__":
    main()
