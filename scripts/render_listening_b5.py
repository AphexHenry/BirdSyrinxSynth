"""Render WP-B5 listening examples (two sides, gating, coupling, side switching)."""

from __future__ import annotations

import json
import math
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
RENDER = ROOT / "build" / "release" / "bin" / "bird_render"
OUT = ROOT / "listening" / "B5"
ASSETS = ROOT / "agent documents" / "reports" / "assets" / "B5"
FIGS = OUT / "figs"

ALPHA_REST = -0.10
ALPHA_MAX = 0.45
LISTEN_R = 0.9
LISTEN_LOSS = 0.0
GAMMA = 24000.0
SRC_RATE = 192000.0


def pressure_for_alpha(alpha: float) -> float:
    return float((alpha - ALPHA_REST) / (ALPHA_MAX - ALPHA_REST))


def _cc():
    import cuicui_core as cc

    return cc


def _zc_f0(x: np.ndarray, sr: float, skip_s: float = 0.05) -> float:
    y = np.asarray(x, dtype=np.float64)
    i0 = int(round(skip_s * sr))
    if y.size - i0 < 32:
        return 0.0
    sl = y[i0:]
    mu = float(np.mean(sl))
    z = sl - mu
    times: list[float] = []
    for i in range(1, z.size):
        a = z[i - 1]
        b = z[i]
        if a <= 0.0 < b:
            frac = -a / (b - a)
            times.append((i0 + i - 1 + frac) / sr)
    if len(times) < 4:
        return 0.0
    dt = np.diff(times)
    dt = dt[(dt > 1e-5) & (dt < 0.02)]
    if dt.size < 3:
        return 0.0
    return float(1.0 / np.mean(dt))


def f0_of_beta(alpha: float, beta: float) -> float:
    cc = _cc()
    n = int(0.12 * SRC_RATE)
    x0, y0 = cc.rest_state(alpha, beta)
    a = np.full(n, alpha, dtype=np.float64)
    b = np.full(n, beta, dtype=np.float64)
    xs, _ys = cc.source_only(a, b, GAMMA, SRC_RATE, float(x0), float(y0))
    return _zc_f0(np.asarray(xs), SRC_RATE)


def beta_for_f0(alpha: float, target_hz: float) -> tuple[float, float]:
    lo, hi = -0.12, 1.15
    for _ in range(22):
        mid = 0.5 * (lo + hi)
        f = f0_of_beta(alpha, mid)
        if f <= 1.0:
            lo = mid
            continue
        if f < target_hz:
            lo = mid
        else:
            hi = mid
    beta = 0.5 * (lo + hi)
    return beta, f0_of_beta(alpha, beta)


def _render(spec: dict, wav: Path) -> None:
    spec_path = wav.with_suffix(".render.json")
    spec_path.write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
    subprocess.run([str(RENDER), str(spec_path), "-o", str(wav)], check=True)


def _peak_normalize(wav: Path, peak_dbfs: float = -3.0) -> None:
    x, sr = sf.read(wav, dtype="float32")
    peak = float(np.max(np.abs(x)))
    if peak <= 0.0:
        return
    target = 10.0 ** (peak_dbfs / 20.0)
    sf.write(wav, (x * (target / peak)).astype(np.float32), sr, subtype="FLOAT")


def _shared_peak_normalize(wavs: list[Path], peak_dbfs: float = -3.0) -> None:
    arrays: list[tuple[Path, np.ndarray, int]] = []
    peak = 0.0
    for wav in wavs:
        x, sr = sf.read(wav, dtype="float32")
        peak = max(peak, float(np.max(np.abs(x))))
        arrays.append((wav, x, sr))
    if peak <= 0.0:
        return
    target = 10.0 ** (peak_dbfs / 20.0)
    g = target / peak
    for wav, x, sr in arrays:
        sf.write(wav, (x * g).astype(np.float32), sr, subtype="FLOAT")


def _dur(path: Path) -> float:
    info = sf.info(str(path))
    return info.frames / info.samplerate


def _peak_dbfs(path: Path) -> float:
    x, _sr = sf.read(path)
    peak = float(np.max(np.abs(x)))
    return 20.0 * math.log10(peak) if peak > 0 else -120.0


def _labial(
    duration: float,
    *,
    alpha: float,
    beta_l: float,
    beta_r: float,
    gate_l: float,
    gate_r: float,
    extra_auto: dict | None = None,
    extra_params: dict | None = None,
    oec: float = 3500.0,
    gape: float = 0.55,
) -> dict:
    p = pressure_for_alpha(alpha)
    spec: dict = {
        "format": "cuicui.render_spec",
        "version": 1,
        "sample_rate": 48000,
        "duration_s": duration,
        "seed": 1,
        "oversampling": "auto",
        "params": {
            "src.gamma": GAMMA,
            "src.L.beta": beta_l,
            "src.R.beta": beta_r,
            "src.L.gate": gate_l,
            "src.R.gate": gate_r,
            "src.coupling": 0.0,
            "out.gain_db": 0.0,
            "trachea.length_mm": 33.0,
            "trachea.reflection": LISTEN_R,
            "trachea.loss": LISTEN_LOSS,
            "trachea.input_gain": 1.0,
            "oec.freq_hz": oec,
            "oec.damping": 0.35,
            "beak.gape": gape,
            "beak.damping": 0.3,
            "out.radiation": 1.0,
            "noise.beta_sigma": 0.0,
            "noise.aspiration": 0.0,
            "resp.attack_ms": 8.0,
            "resp.release_ms": 15.0,
        },
        "automation": {"resp.pressure": [[0.0, p], [duration, p]]},
        "automation_interp": "linear",
        "output": {"normalize_peak_dbfs": None},
    }
    if extra_params:
        spec["params"].update(extra_params)
    if extra_auto:
        spec["automation"].update(extra_auto)
    return spec


def switcher_tracks(
    duration: float,
    jumps: list[float],
    beta_l: float,
    beta_r: float,
    *,
    ctrl_hz: float = 1000.0,
    crossfade_ms: float = 4.0,
    start_side: int = 0,
) -> dict[str, list[list[float]]]:
    cc = _cc()
    sw = cc.SideSwitcher()
    sw.prepare(ctrl_hz)
    sw.set_crossfade_ms(crossfade_ms)
    n = int(round(duration * ctrl_hz)) + 1
    g_l: list[list[float]] = []
    g_r: list[list[float]] = []
    b_l: list[list[float]] = []
    b_r: list[list[float]] = []
    ji = 0
    active = start_side
    for i in range(n):
        t = i / ctrl_hz
        if ji < len(jumps) and t + 1e-12 >= jumps[ji]:
            active = 1 - active
            sw.set_active(active)
            ji += 1
        bl, br, gl, gr = sw.next(beta_l, beta_r)
        tt = round(t, 6)
        g_l.append([tt, float(gl)])
        g_r.append([tt, float(gr)])
        b_l.append([tt, float(bl)])
        b_r.append([tt, float(br)])
    return {"src.L.gate": g_l, "src.R.gate": g_r, "src.L.beta": b_l, "src.R.beta": b_r}


def glide_beta(duration: float, jumps: list[float], values: list[float], glide_s: float) -> list[list[float]]:
    pts = [[0.0, values[0]]]
    for t, v in zip(jumps, values[1:], strict=True):
        pts.append([round(t, 6), pts[-1][1]])
        pts.append([round(min(duration, t + glide_s), 6), v])
    pts.append([duration, values[-1]])
    return pts


def spectro_pair(a: Path, b: Path, out: Path, title_a: str, title_b: str) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 2, figsize=(9.2, 3.6), sharey=True)
    for ax, wav, title in ((axes[0], a, title_a), (axes[1], b, title_b)):
        x, sr = sf.read(wav, dtype="float32")
        nfft = 1024
        hop = 128
        ax.specgram(x, NFFT=nfft, Fs=sr, noverlap=nfft - hop, cmap="magma", vmin=-80, vmax=0)
        ax.set_title(title)
        ax.set_xlabel("time (s)")
        ax.set_ylim(0, 12000)
    axes[0].set_ylabel("Hz")
    fig.tight_layout()
    fig.savefig(out, dpi=140, bbox_inches="tight")
    plt.close(fig)


def spectro_one(wav: Path, out: Path, title: str) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    x, sr = sf.read(wav, dtype="float32")
    fig, ax = plt.subplots(figsize=(7.2, 3.4))
    ax.specgram(x, NFFT=1024, Fs=sr, noverlap=1024 - 128, cmap="magma", vmin=-80, vmax=0)
    ax.set_title(title)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("Hz")
    ax.set_ylim(0, 12000)
    fig.tight_layout()
    fig.savefig(out, dpi=140, bbox_inches="tight")
    plt.close(fig)


def repeat_phrase(
    duration_one: float,
    n_rep: int,
    gap_s: float,
    make_auto: callable,
) -> tuple[float, dict]:
    """Tile a short automation phrase `n_rep` times with silent gaps (pressure 0)."""
    cycle = duration_one + gap_s
    total = n_rep * cycle
    merged: dict[str, list[list[float]]] = {}
    press: list[list[float]] = [[0.0, 0.0]]
    p_on = None
    for k in range(n_rep):
        t0 = k * cycle
        auto = make_auto(duration_one)
        if p_on is None:
            p_on = auto["resp.pressure"][0][1]
        press += [[t0, 0.0], [t0 + 0.004, p_on], [t0 + duration_one, p_on], [t0 + duration_one + 0.01, 0.0]]
        for key, pts in auto.items():
            if key == "resp.pressure":
                continue
            shifted = [[round(t0 + t, 6), v] for t, v in pts]
            merged.setdefault(key, []).extend(shifted)
    press.append([total, 0.0])
    merged["resp.pressure"] = press
    return total, merged


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    FIGS.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)

    alpha_tv = 0.20
    b_l1800, f_l1800 = beta_for_f0(alpha_tv, 1800.0)
    b_r2400, f_r2400 = beta_for_f0(alpha_tv, 2400.0)
    b_beat0, f_beat0 = beta_for_f0(alpha_tv, 1800.0)
    b_beat1, f_beat1 = beta_for_f0(alpha_tv, 1808.0)

    alpha_jump = 0.16
    b_low, f_low = beta_for_f0(alpha_jump, 1500.0)
    b_high, f_high = beta_for_f0(alpha_jump, 3500.0)

    alpha_wide = 0.18
    b_6k, f_6k = beta_for_f0(alpha_wide, 6000.0)
    b_3k, f_3k = beta_for_f0(alpha_wide, 3000.0)
    b_12k, f_12k = beta_for_f0(alpha_wide, 1200.0)

    alpha_trill = 0.16
    b_oct_lo, f_oct_lo = beta_for_f0(alpha_trill, 1600.0)
    b_oct_hi, f_oct_hi = beta_for_f0(alpha_trill, 3200.0)

    pitch_map = {
        "two_voice": {
            "alpha": alpha_tv,
            "L_beta": round(b_l1800, 5),
            "R_beta": round(b_r2400, 5),
            "L_f0": round(f_l1800, 1),
            "R_f0": round(f_r2400, 1),
        },
        "beating": {
            "alpha": alpha_tv,
            "L_beta": round(b_beat0, 5),
            "R_beta": round(b_beat1, 5),
            "L_f0": round(f_beat0, 1),
            "R_f0": round(f_beat1, 1),
            "df_hz": round(f_beat1 - f_beat0, 2),
        },
        "jump": {
            "alpha": alpha_jump,
            "low_beta": round(b_low, 5),
            "high_beta": round(b_high, 5),
            "low_f0": round(f_low, 1),
            "high_f0": round(f_high, 1),
        },
        "sweep": {
            "alpha": alpha_wide,
            "f_top": round(f_6k, 1),
            "f_mid": round(f_3k, 1),
            "f_bot": round(f_12k, 1),
        },
        "trill": {
            "alpha": alpha_trill,
            "low_f0": round(f_oct_lo, 1),
            "high_f0": round(f_oct_hi, 1),
        },
    }
    (OUT / "pitch_map.json").write_text(json.dumps(pitch_map, indent=2) + "\n", encoding="utf-8")
    print("pitch_map", json.dumps(pitch_map))

    # 01–03 two voices
    spec01 = _labial(1.4, alpha=alpha_tv, beta_l=b_l1800, beta_r=b_r2400, gate_l=1.0, gate_r=0.0)
    spec02 = _labial(1.4, alpha=alpha_tv, beta_l=b_l1800, beta_r=b_r2400, gate_l=0.0, gate_r=1.0)
    spec03 = _labial(1.4, alpha=alpha_tv, beta_l=b_l1800, beta_r=b_r2400, gate_l=1.0, gate_r=1.0)
    _render(spec01, OUT / "01_left_only.wav")
    _render(spec02, OUT / "02_right_only.wav")
    _render(spec03, OUT / "03_both_two_voice.wav")
    _shared_peak_normalize([OUT / "01_left_only.wav", OUT / "02_right_only.wav", OUT / "03_both_two_voice.wav"])

    # 04 beating
    spec04 = _labial(3.0, alpha=alpha_tv, beta_l=b_beat0, beta_r=b_beat1, gate_l=1.0, gate_r=1.0)
    _render(spec04, OUT / "04_beating.wav")
    _peak_normalize(OUT / "04_beating.wav")

    # 05 coupling sweep, same detuning
    spec05 = _labial(
        5.0,
        alpha=alpha_tv,
        beta_l=b_beat0,
        beta_r=b_beat1,
        gate_l=1.0,
        gate_r=1.0,
        extra_auto={"src.coupling": [[0.0, 0.0], [5.0, 0.1]]},
    )
    _render(spec05, OUT / "05_coupling_sweep.wav")
    _peak_normalize(OUT / "05_coupling_sweep.wav")

    # 06 single-side β glide vs 07 side switch
    jump_t = [0.25, 0.50, 0.75, 1.00, 1.25]
    jump_dur = 1.55
    jump_vals = [b_low, b_high, b_low, b_high, b_low, b_high]
    spec06 = _labial(
        jump_dur,
        alpha=alpha_jump,
        beta_l=b_low,
        beta_r=b_high,
        gate_l=1.0,
        gate_r=0.0,
        extra_auto={"src.L.beta": glide_beta(jump_dur, jump_t, jump_vals, 0.09)},
    )
    tracks07 = switcher_tracks(jump_dur, jump_t, b_low, b_high)
    spec07 = _labial(
        jump_dur,
        alpha=alpha_jump,
        beta_l=b_low,
        beta_r=b_high,
        gate_l=1.0,
        gate_r=0.0,
        extra_auto=tracks07,
    )
    _render(spec06, OUT / "06_jump_glide_single_side.wav")
    _render(spec07, OUT / "07_jump_side_switch.wav")
    _peak_normalize(OUT / "06_jump_glide_single_side.wav")
    _peak_normalize(OUT / "07_jump_side_switch.wav")

    # 08 octave trill, 12 Hz alternation
    trill_dur = 1.6
    trill_jumps = [round(i / 24.0, 6) for i in range(1, int(trill_dur * 24))]
    tracks08 = switcher_tracks(trill_dur, trill_jumps, b_oct_lo, b_oct_hi)
    spec08 = _labial(
        trill_dur,
        alpha=alpha_trill,
        beta_l=b_oct_lo,
        beta_r=b_oct_hi,
        gate_l=1.0,
        gate_r=0.0,
        extra_auto=tracks08,
    )
    _render(spec08, OUT / "08_octave_trill_switch.wav")
    _peak_normalize(OUT / "08_octave_trill_switch.wav")

    # 09 broad downsweep, R top / L bottom, 4 repeats
    def make_sweep(d: float) -> dict:
        mid = 0.075
        auto = switcher_tracks(d, [mid], b_12k, b_6k, start_side=1)
        auto["src.R.beta"] = [[0.0, b_6k], [mid, b_3k], [d, b_3k]]
        auto["src.L.beta"] = [[0.0, b_3k], [mid, b_3k], [d, b_12k]]
        auto["oec.freq_hz"] = [[0.0, max(f_6k, 4000.0)], [d, max(f_12k, 1400.0)]]
        auto["resp.pressure"] = [[0.0, pressure_for_alpha(alpha_wide)], [d, pressure_for_alpha(alpha_wide)]]
        return auto

    total09, auto09 = repeat_phrase(0.15, 4, 0.12, make_sweep)
    spec09 = _labial(
        total09,
        alpha=alpha_wide,
        beta_l=b_3k,
        beta_r=b_6k,
        gate_l=0.0,
        gate_r=1.0,
        extra_auto=auto09,
        extra_params={"resp.attack_ms": 4.0, "resp.release_ms": 10.0},
    )
    _render(spec09, OUT / "09_broad_downsweep_split.wav")
    _peak_normalize(OUT / "09_broad_downsweep_split.wav")

    spectro_pair(
        OUT / "06_jump_glide_single_side.wav",
        OUT / "07_jump_side_switch.wav",
        FIGS / "spec_06_vs_07.png",
        "06 one-side β glide",
        "07 side switch",
    )
    spectro_one(OUT / "09_broad_downsweep_split.wav", FIGS / "spec_09.png", "09 split downsweep")
    lock_png = FIGS / "locking.png"
    if not lock_png.is_file():
        subprocess.run(
            [
                str(ROOT / "python" / ".venv" / "bin" / "python"),
                "-m",
                "cuicui.analysis.locking",
                "--out",
                str(FIGS),
            ],
            cwd=str(ROOT / "python"),
            check=True,
        )

    copied: list[str] = []
    for name in ("locking.png", "spec_06_vs_07.png", "spec_09.png"):
        src = FIGS / name
        if src.is_file():
            shutil.copy2(src, ASSETS / name)
            copied.append(name)

    def metrics(name: str, extra: dict | None = None) -> dict:
        p = OUT / name
        m = {"duration_s": round(_dur(p), 4), "peak_dbfs": round(_peak_dbfs(p), 2)}
        if extra:
            m.update(extra)
        return m

    items = [
        {
            "file": "01_left_only.wav",
            "title": "Left side only, ~1.8 kHz",
            "listen_for": "A single whistle. Compare with 02: same air, other side, higher pitch.",
            "params": {
                "src.L.gate": 1,
                "src.R.gate": 0,
                "src.L.beta": round(b_l1800, 4),
                "f0_L_hz": round(f_l1800, 1),
            },
            "metrics": metrics("01_left_only.wav"),
            "group": "two voices",
            "reference": None,
        },
        {
            "file": "02_right_only.wav",
            "title": "Right side only, ~2.4 kHz",
            "listen_for": "A single higher whistle (about a perfect fourth above 01). Closed left should add no extra pitch.",
            "params": {
                "src.L.gate": 0,
                "src.R.gate": 1,
                "src.R.beta": round(b_r2400, 4),
                "f0_R_hz": round(f_r2400, 1),
            },
            "metrics": metrics("02_right_only.wav"),
            "group": "two voices",
            "reference": None,
        },
        {
            "file": "03_both_two_voice.wav",
            "title": "Both sides, two voices",
            "listen_for": "Two independent pitches at once (1.8 + 2.4 kHz), not a harmonic stack of one tone.",
            "params": {
                "src.L.gate": 1,
                "src.R.gate": 1,
                "src.coupling": 0,
            },
            "metrics": metrics("03_both_two_voice.wav"),
            "group": "two voices",
            "reference": None,
        },
        {
            "file": "04_beating.wav",
            "title": "Close detune, no coupling",
            "listen_for": f"Slow beating (~{abs(f_beat1 - f_beat0):.1f} Hz): loud–soft–loud, two nearly unison whistles.",
            "params": {
                "src.coupling": 0,
                "df_hz": round(f_beat1 - f_beat0, 2),
            },
            "metrics": metrics("04_beating.wav"),
            "group": None,
            "reference": None,
        },
        {
            "file": "05_coupling_sweep.wav",
            "title": "Same detune, coupling 0 → 0.1",
            "listen_for": "Starts like 04. If the sides lock, beating should collapse to one pitch before 5 s. Lock range is small — it may only thin, not freeze.",
            "params": {"src.coupling": "0→0.1", "df_hz": round(f_beat1 - f_beat0, 2)},
            "metrics": metrics("05_coupling_sweep.wav"),
            "group": None,
            "reference": None,
        },
        {
            "file": "06_jump_glide_single_side.wav",
            "title": "Pitch jumps on one side (β glide)",
            "listen_for": "Every 250 ms the pitch swoops between ~1.5 and ~3.5 kHz. You should hear the in-between notes.",
            "params": {
                "alpha": alpha_jump,
                "glide_ms": 90,
                "f_low_hz": round(f_low, 1),
                "f_high_hz": round(f_high, 1),
            },
            "metrics": metrics("06_jump_glide_single_side.wav"),
            "group": "jumps",
            "reference": None,
        },
        {
            "file": "07_jump_side_switch.wav",
            "title": "Same jumps, side switch (4 ms)",
            "listen_for": "Same rhythm as 06, but each jump should click from low to high with no swoop. The unused side is already in tune.",
            "params": {
                "crossfade_ms": 4.0,
                "alpha": alpha_jump,
                "f_low_hz": round(f_low, 1),
                "f_high_hz": round(f_high, 1),
            },
            "metrics": metrics("07_jump_side_switch.wav"),
            "group": "jumps",
            "reference": None,
        },
        {
            "file": "08_octave_trill_switch.wav",
            "title": "12 Hz octave trill by switching",
            "listen_for": "A crisp two-level trill (octave), not a smear. About 12 flips per second.",
            "params": {
                "rate_hz": 12,
                "f_low_hz": round(f_oct_lo, 1),
                "f_high_hz": round(f_oct_hi, 1),
            },
            "metrics": metrics("08_octave_trill_switch.wav"),
            "group": None,
            "reference": None,
        },
        {
            "file": "09_broad_downsweep_split.wav",
            "title": "Broad downsweep, R then L",
            "listen_for": f"Four short falling cries, ~{f_6k:.0f} → {f_12k:.0f} Hz (6 kHz is above this α's ceiling). Tiny seam near 3 kHz where R hands to L, not a restart.",
            "params": {
                "f_top_hz": round(f_6k, 1),
                "f_mid_hz": round(f_3k, 1),
                "f_bot_hz": round(f_12k, 1),
                "target_hz": "6000→1200",
                "reached_top_hz": round(f_6k, 1),
            },
            "metrics": metrics("09_broad_downsweep_split.wav", {"repeats": 4}),
            "group": None,
            "reference": None,
        },
    ]

    figures = [
        {
            "file": "figs/locking.png",
            "title": "Locking vs coupling",
            "caption": "Largest Δβ that still locks (single f₀) for ε = 0.01, 0.03, 0.1. Must be monotone in ε; no pass threshold.",
        },
        {
            "file": "figs/spec_06_vs_07.png",
            "title": "06 glide vs 07 switch",
            "caption": "Left: continuous f₀ swoop. Right: hop over the 1.8–3.2 kHz gap.",
        },
        {
            "file": "figs/spec_09.png",
            "title": "09 split downsweep",
            "caption": "R covers the top, L the bottom; crossover near 3 kHz.",
        },
    ]

    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "B5",
        "title": "Two sources — left/right labia, gating, side switching",
        "figures": figures,
        "items": items,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("wrote", OUT / "manifest.json", "copied", copied)


if __name__ == "__main__":
    main()
