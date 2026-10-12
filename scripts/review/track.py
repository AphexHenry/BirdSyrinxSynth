"""Shared analysis: dominant-trace f0/amplitude tracker + birdness metrics.

Moved to `python/cuicui/analysis/f0_track.py` in WP-R1 (the bank builder needs it); this module
re-exports it so the R0 review and study scripts keep working unchanged.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from cuicui.analysis.f0_track import HOP_S, metrics, runs, stft_mag, track  # noqa: E402,F401
from cuicui.analysis.f0_track import _clean_runs  # noqa: E402,F401
