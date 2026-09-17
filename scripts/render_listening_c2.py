"""Render WP-C2 listening examples (inverse-map tuning)."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

os.environ.setdefault(
    "MPLCONFIGDIR", str(Path(__file__).resolve().parents[1] / "listening" / "C2" / ".mplconfig")
)
Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
OUT = ROOT / "listening" / "C2"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "C2"
FIGS = OUT / "figs"
ATLAS = ROOT / "assets" / "atlas"

from cuicui.atlas.inverse import load_atlas_bundle, midi_from_hz  # noqa: E402
from cuicui.atlas.source_atlas import GAMMA0  # noqa: E402
from cuicui.listen.manifest import load_manifest  # noqa: E402


def _cc():
    import cuicui_core as cc

    return cc


def _peak_norm(y: np.ndarray, peak_db: float = -3.0) -> np.ndarray:
    peak = float(np.max(np.abs(y))) if y.size else 0.0
    if peak <= 1e-12:
        return y.astype(np.float32)
    g = (10.0 ** (peak_db / 20.0)) / peak
    return (y * g).astype(np.float32)


def _alpha_controls(alpha: float, n: int) -> tuple[np.ndarray, np.ndarray]:
    if alpha <= 0.45:
        p = np.full(n, (alpha + 0.10) / 0.55, dtype=np.float32)
        off = np.zeros(n, dtype=np.float32)
    else:
        p = np.ones(n, dtype=np.float32)
        off = np.full(n, alpha - 0.45, dtype=np.float32)
    return p, off


def _f0_near(x: np.ndarray, sr: float, f_ref: float, skip_s: float = 0.08) -> float:
    y = np.asarray(x, dtype=np.float64)
    i0 = int(round(skip_s * sr))
    sl = y[i0:] - np.mean(y[i0:]) if y.size > i0 + 32 else y
    if sl.size < 64 or not (f_ref > 1.0):
        return 0.0
    nfft = 1 << int(np.ceil(np.log2(max(int(sl.size), 4096))))
    nfft = min(nfft, 16384)
    w = np.hanning(sl.size)
    spec = np.abs(np.fft.rfft(sl * w, n=nfft))
    freqs = np.fft.rfftfreq(nfft, d=1.0 / sr)
    lo = f_ref * 0.82
    hi = f_ref * 1.22
    mask = (freqs >= lo) & (freqs <= hi)
    if not np.any(mask):
        return 0.0
    idx = np.nonzero(mask)[0]
    k = int(idx[int(np.argmax(spec[mask]))])
    if 0 < k < spec.size - 1:
        a, b, c = float(spec[k - 1]), float(spec[k]), float(spec[k + 1])
        denom = a - 2.0 * b + c
        delta = 0.5 * (a - c) / denom if abs(denom) > 1e-18 else 0.0
        delta = float(np.clip(delta, -0.5, 0.5))
        return (k + delta) * sr / nfft
    return float(k * sr / nfft)


def _render_note(
    cc,
    alpha: float,
    beta: float,
    n: int,
    sr: float,
    sigma: float,
    pressure: np.ndarray | None = None,
) -> np.ndarray:
    if pressure is None:
        p, off = _alpha_controls(alpha, n)
    else:
        p = np.asarray(pressure, dtype=np.float32)
        off = np.zeros(n, dtype=np.float32)
        if alpha > 0.45:
            off = np.full(n, alpha - 0.45, dtype=np.float32)
    settings = {
        "body.size": float(sigma),
        "noise.aspiration": 0.0,
        "out.gain_db": -6.0,
    }
    controls = {
        "resp.pressure": p,
        "src.L.beta": np.full(n, beta, dtype=np.float32),
        "src.R.beta": np.full(n, beta, dtype=np.float32),
        "src.L.gate": np.ones(n, dtype=np.float32),
        "src.R.gate": np.zeros(n, dtype=np.float32),
        "src.L.alpha_offset": off,
        "noise.beta_sigma": np.zeros(n, dtype=np.float32),
    }
    y, _ = cc.render_controls(settings, controls, sr, sr, 1, 0)
    return np.asarray(y, dtype=np.float32)


def _solve(cc, f0: float, tone: float, sigma: float, range_fold: bool = False) -> dict:
    gamma = GAMMA0 / sigma
    nu = 2.0 * np.pi * f0 / gamma
    return cc.inverse_solve(float(nu), float(tone), 0.25, 0.0, bool(range_fold))


def _midi_range(cc, tone: float, sigma: float) -> tuple[int, int]:
    nmin, nmax = cc.inverse_nu_range(tone)
    gamma = GAMMA0 / sigma
    f_lo = nmin * gamma / (2.0 * np.pi)
    f_hi = nmax * gamma / (2.0 * np.pi)
    m0 = int(np.ceil(midi_from_hz(f_lo) - 1e-9))
    m1 = int(np.floor(midi_from_hz(f_hi) + 1e-9))
    return m0, m1


def _chromatic(cc, tone: float, sigma: float = 1.0) -> tuple[np.ndarray, list[float]]:
    sr = 48000.0
    note_n = int(round(0.30 * sr))
    gap_n = int(round(0.08 * sr))
    m0, m1 = _midi_range(cc, tone, sigma)
    parts: list[np.ndarray] = []
    cents: list[float] = []
    for m in range(m0, m1 + 1):
        f_ref = 440.0 * 2.0 ** ((m - 69) / 12.0)
        sol = _solve(cc, f_ref, tone, sigma, False)
        y = _render_note(cc, sol["alpha"], sol["beta"], note_n, sr, sigma)
        f_meas = _f0_near(y, sr, f_ref)
        c = 1200.0 * np.log2(f_meas / f_ref) if f_meas > 0 else 999.0
        cents.append(float(c))
        parts.append(y)
        parts.append(np.zeros(gap_n, dtype=np.float32))
    audio = np.concatenate(parts) if parts else np.zeros(1, dtype=np.float32)
    return audio, cents


def _frere_jacques_midi(tonic: int) -> list[int]:
    # C D E C | C D E C | E F G | G A G F E C  relative to tonic = C.
    rel = [0, 2, 4, 0, 0, 2, 4, 0, 4, 5, 7, 7, 9, 7, 5, 4, 0]
    return [tonic + r for r in rel]


def _melody(cc, sigma: float, tone: float = 0.6) -> np.ndarray:
    sr = 48000.0
    note_n = int(round(0.28 * sr))
    gap_n = int(round(0.06 * sr))
    m0, m1 = _midi_range(cc, tone, sigma)
    tonic = max(m0, min(m1 - 12, 84))  # prefer C6=84 if in range
    if tonic < m0:
        tonic = m0
    parts = []
    for m in _frere_jacques_midi(tonic):
        m = int(np.clip(m, m0, m1))
        f_ref = 440.0 * 2.0 ** ((m - 69) / 12.0)
        sol = _solve(cc, f_ref, tone, sigma, False)
        y = _render_note(cc, sol["alpha"], sol["beta"], note_n, sr, sigma)
        parts.append(y)
        parts.append(np.zeros(gap_n, dtype=np.float32))
    return np.concatenate(parts)


def main() -> None:
    cc = _cc()
    OUT.mkdir(parents=True, exist_ok=True)
    FIGS.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)
    sr = 48000

    items = []
    all_cents: dict[str, list[float]] = {}
    for fname, tone, title in (
        ("01_chromatic_tone02.wav", 0.2, "Chromatic, tone 0.2 (rich)"),
        ("02_chromatic_tone06.wav", 0.6, "Chromatic, tone 0.6"),
        ("03_chromatic_tone10.wav", 1.0, "Chromatic, tone 1.0 (whistle)"),
    ):
        audio, cents = _chromatic(cc, tone, 1.0)
        all_cents[fname] = cents
        sf.write(OUT / fname, _peak_norm(audio), sr)
        med = float(np.median(np.abs(cents))) if cents else 0.0
        mx = float(np.max(np.abs(cents))) if cents else 0.0
        items.append(
            {
                "file": fname,
                "title": title,
                "listen_for": "In tune; timbre from rich/reedy (0.2) to whistle (1.0).",
                "params": {"perf.tone": tone, "body.size": 1.0},
                "metrics": {
                    "median_abs_cents": round(med, 2),
                    "max_abs_cents": round(mx, 2),
                    "n_notes": len(cents),
                },
                "group": "chromatic",
            }
        )

    # 04 tone morph at ~2 kHz, 6 s, tone 0 → 1 → 0 (continuous controls, no re-attack)
    cr = 200.0
    dur = 6.0
    n_c = int(round(dur * cr))
    t_c = np.arange(n_c, dtype=np.float64) / cr
    tone_c = np.where(t_c < 3.0, t_c / 3.0, 1.0 - (t_c - 3.0) / 3.0)
    f_ref = 2000.0
    nu = 2.0 * np.pi * f_ref / GAMMA0
    a_c = np.empty(n_c, dtype=np.float32)
    b_c = np.empty(n_c, dtype=np.float32)
    for i in range(n_c):
        sol = cc.inverse_solve(nu, float(tone_c[i]), 0.25, 0.0, False)
        a_c[i] = sol["alpha"]
        b_c[i] = sol["beta"]
    p_c = np.where(a_c <= 0.45, (a_c + 0.10) / 0.55, 1.0).astype(np.float32)
    off_c = np.where(a_c <= 0.45, 0.0, a_c - 0.45).astype(np.float32)
    settings = {"body.size": 1.0, "noise.aspiration": 0.0, "out.gain_db": -6.0}
    controls = {
        "resp.pressure": p_c,
        "src.L.beta": b_c,
        "src.R.beta": b_c,
        "src.L.gate": np.ones(n_c, dtype=np.float32),
        "src.R.gate": np.zeros(n_c, dtype=np.float32),
        "src.L.alpha_offset": off_c,
        "noise.beta_sigma": np.zeros(n_c, dtype=np.float32),
    }
    morph, _ = cc.render_controls(settings, controls, cr, float(sr), 1, 0)
    morph = np.asarray(morph, dtype=np.float32)
    sf.write(OUT / "04_tone_morph_fixed_pitch.wav", _peak_norm(morph), sr)
    n = morph.size
    f_chunks = [
        _f0_near(morph[i : i + sr // 2], float(sr), f_ref, 0.02)
        for i in range(0, n - sr // 2, sr // 2)
    ]
    f_chunks = [f for f in f_chunks if f > 0]
    drift = 1200.0 * np.log2(max(f_chunks) / min(f_chunks)) if len(f_chunks) >= 2 else 0.0
    items.append(
        {
            "file": "04_tone_morph_fixed_pitch.wav",
            "title": "Tone morph at fixed pitch (2 kHz)",
            "listen_for": "Pitch stays still while the timbre morphs rich → whistle → rich.",
            "params": {"f0_hz": 2000.0},
            "metrics": {"pitch_drift_cents_span": round(float(drift), 2)},
        }
    )

    # 05 physics = 1, breath oscillating (continuous)
    cr = 200.0
    dur = 4.0
    n_c = int(round(dur * cr))
    breath = 0.25 + 0.25 * np.sin(2.0 * np.pi * 0.4 * np.arange(n_c) / cr)
    f_ref = 2000.0
    nu = 2.0 * np.pi * f_ref / GAMMA0
    a_c = np.empty(n_c, dtype=np.float32)
    b_c = np.empty(n_c, dtype=np.float32)
    for i in range(n_c):
        sol = cc.inverse_solve(nu, 0.6, float(breath[i]), 1.0, False)
        a_c[i] = sol["alpha"]
        b_c[i] = sol["beta"]
    p_c = np.where(a_c <= 0.45, (a_c + 0.10) / 0.55, 1.0).astype(np.float32)
    off_c = np.where(a_c <= 0.45, 0.0, a_c - 0.45).astype(np.float32)
    controls = {
        "resp.pressure": p_c,
        "src.L.beta": b_c,
        "src.R.beta": b_c,
        "src.L.gate": np.ones(n_c, dtype=np.float32),
        "src.R.gate": np.zeros(n_c, dtype=np.float32),
        "src.L.alpha_offset": off_c,
        "noise.beta_sigma": np.zeros(n_c, dtype=np.float32),
    }
    phys, _ = cc.render_controls(settings, controls, cr, float(sr), 1, 0)
    phys = np.asarray(phys, dtype=np.float32)
    sf.write(OUT / "05_physics_breath.wav", _peak_norm(phys), sr)
    items.append(
        {
            "file": "05_physics_breath.wav",
            "title": "Physics = 1, slow breath",
            "listen_for": "Timbre breathes with pressure; pitch stays put.",
            "params": {"perf.physics": 1.0, "f0_hz": 2000.0},
            "metrics": {"duration_s": 4.0},
        }
    )

    for fname, sigma, title in (
        ("06_melody_size1.wav", 1.0, "Frère Jacques, size 1"),
        ("07_melody_size2.wav", 2.0, "Frère Jacques, size 2 (bigger bird)"),
    ):
        y = _melody(cc, sigma)
        sf.write(OUT / fname, _peak_norm(y), sr)
        items.append(
            {
                "file": fname,
                "title": title,
                "listen_for": "Recognisable tune in a bird voice. Size 2 is lower and larger.",
                "params": {"body.size": sigma},
                "metrics": {"sigma": sigma},
                "group": "melody",
            }
        )

    # 08 folding: scale from a 5th below max to an octave above
    nmin, nmax = cc.inverse_nu_range(0.6)
    gamma = GAMMA0
    f_max = nmax * gamma / (2.0 * np.pi)
    m_max = int(round(midi_from_hz(f_max)))
    notes = list(range(m_max - 7, m_max + 13))
    note_n = int(round(0.25 * sr))
    gap_n = int(round(0.06 * sr))
    parts = []
    n_folded = 0
    for m in notes:
        f_ref = 440.0 * 2.0 ** ((m - 69) / 12.0)
        sol = _solve(cc, f_ref, 0.6, 1.0, True)
        if sol["folded"]:
            n_folded += 1
        y = _render_note(cc, sol["alpha"], sol["beta"], note_n, float(sr), 1.0)
        parts.append(y)
        parts.append(np.zeros(gap_n, dtype=np.float32))
    sf.write(OUT / "08_folding.wav", _peak_norm(np.concatenate(parts)), sr)
    items.append(
        {
            "file": "08_folding.wav",
            "title": "Scale through the top of the range (folding on)",
            "listen_for": "Notes above the range drop an octave instead of failing.",
            "params": {"perf.range_fold": 1.0},
            "metrics": {"n_folded": n_folded, "n_notes": len(notes)},
        }
    )

    # Tuning-error figure
    fig, ax = plt.subplots(figsize=(7.2, 4.2), dpi=120)
    for fname, label in (
        ("01_chromatic_tone02.wav", "tone 0.2"),
        ("02_chromatic_tone06.wav", "tone 0.6"),
        ("03_chromatic_tone10.wav", "tone 1.0"),
    ):
        ax.plot(all_cents[fname], lw=1.2, label=label)
    ax.axhline(0.0, color="k", lw=0.6)
    ax.axhline(5.0, color="0.6", ls="--", lw=0.8)
    ax.axhline(-5.0, color="0.6", ls="--", lw=0.8)
    ax.set_xlabel("semitone index (playable range)")
    ax.set_ylabel("cents error")
    ax.set_title("Closed-loop tuning error (σ = 1)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(FIGS / "tuning_error.png")
    plt.close(fig)

    meta, _arrays = load_atlas_bundle(ATLAS)
    range_rows = meta["playable_range"]
    fig, ax = plt.subplots(figsize=(7.2, 3.6), dpi=120)
    ax.axis("off")
    table = [[f"σ={r['sigma']:g}", f"tone {r['tone']}", r["label"]] for r in range_rows]
    ax.table(cellText=table, colLabels=["size", "tone", "playable"], loc="center")
    ax.set_title("Playable range")
    fig.tight_layout()
    fig.savefig(FIGS / "playable_range.png")
    plt.close(fig)

    import shutil

    for src in (
        FIGS / "regime_map.png",
        FIGS / "inverse_beta.png",
        FIGS / "tuning_error.png",
        FIGS / "playable_range.png",
    ):
        if src.is_file():
            shutil.copy2(src, ASSETS / src.name)

    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "C2",
        "title": "Source atlas & inverse pitch map",
        "items": items,
        "figures": [
            {
                "file": "figs/regime_map.png",
                "title": "Atlas f0 with onset/offset",
                "caption": "From-rest protocol.",
            },
            {
                "file": "figs/inverse_beta.png",
                "title": "Inverse β(α, ν)",
                "caption": "Tone runs from α_lo to α_hi.",
            },
            {
                "file": "figs/tuning_error.png",
                "title": "Closed-loop cents error",
                "caption": "σ = 1 chromatic.",
            },
            {
                "file": "figs/playable_range.png",
                "title": "Playable range",
                "caption": "Note names vs size and tone.",
            },
        ],
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    load_manifest(OUT / "manifest.json")
    print("C2 listening renders OK")


if __name__ == "__main__":
    main()
