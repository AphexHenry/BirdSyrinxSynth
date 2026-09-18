"""Helpers to compose gesture scores from archetypes (WP-C4)."""

from __future__ import annotations

from typing import Any

try:
    import cuicui_core as cc
except ImportError:  # pragma: no cover
    cc = None  # type: ignore[assignment]


def _core():
    if cc is None:
        raise ImportError("cuicui_core is not built (run scripts/build.sh)")
    return cc


def archetype_event(
    name: str,
    t_s: float,
    *,
    params: dict[str, Any] | None = None,
    anchor_hz: float = 2093.0,
    seed: int = 1,
) -> dict[str, Any]:
    """One gesture-score event generated from a named archetype."""
    core = _core()
    gen = core.generate_archetype(name, params or {}, seed)
    return {
        "t_s": float(t_s),
        "duration_ms": float(gen["duration_ms"]),
        "type": f"archetype:{name}",
        "anchor_hz": float(anchor_hz),
        "params": dict(params or {}),
        "hints": dict(gen["hints"]),
        "curves": dict(gen["curves"]),
    }


def sequence(
    items: list[tuple[str, dict[str, Any] | None, float] | tuple[str, dict[str, Any] | None]],
    *,
    gap_s: float = 0.06,
    anchor_hz: float = 2093.0,
    seed: int = 1,
    name: str = "",
    settings: dict[str, float] | None = None,
    gaps: list[float] | None = None,
) -> dict[str, Any]:
    """Build a gesture_score dict from (archetype, params) or (archetype, params, anchor_hz).

    Events are laid out in order. `gaps[i]` (seconds) is the silence after event i;
    missing entries fall back to `gap_s`.
    """
    gestures: list[dict[str, Any]] = []
    t = 0.05
    for i, item in enumerate(items):
        aname = item[0]
        params = item[1]
        ahz = float(item[2]) if len(item) == 3 else float(anchor_hz)
        ev = archetype_event(aname, t, params=params, anchor_hz=ahz, seed=seed + i)
        gestures.append(ev)
        after = gap_s
        if gaps is not None and i < len(gaps):
            after = float(gaps[i])
        t += float(ev["duration_ms"]) * 0.001 + after
    return {
        "format": "cuicui.gesture_score",
        "version": 1,
        "name": name,
        "created_by": "WP-C4",
        "created_at": "2026-09-18T00:00:00Z",
        "settings": dict(settings or {"body.size": 1.0, "perf.breath": 0.7}),
        "gestures": gestures,
        "segments": [],
    }


def physio_score(
    tracks: dict[str, list[float]],
    *,
    control_rate_hz: float = 1000.0,
    settings: dict[str, float] | None = None,
    name: str = "",
) -> dict[str, Any]:
    """Dense physiological-track score (bit-identical to render_controls)."""
    return {
        "format": "cuicui.gesture_score",
        "version": 1,
        "name": name,
        "created_by": "WP-C4",
        "created_at": "2026-09-18T00:00:00Z",
        "settings": dict(settings or {"body.size": 1.0}),
        "physio": {
            "control_rate_hz": float(control_rate_hz),
            "t0_s": 0.0,
            "tracks": {k: [float(x) for x in v] for k, v in tracks.items()},
        },
        "gestures": [],
        "segments": [],
    }
