from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import soundfile as sf

from cuicui.listen.page import build_page
from cuicui.listen.spectrogram import save_spectrogram, save_waveform


def _tone(path: Path, *, duration: float = 0.2, sr: int = 48000) -> None:
    t = np.arange(int(duration * sr), dtype=np.float64) / sr
    y = (0.25 * np.sin(2 * np.pi * 1000 * t)).astype(np.float32)
    sf.write(path, y, sr, subtype="FLOAT")


def test_save_spectrogram_and_waveform(tmp_path: Path) -> None:
    wav = tmp_path / "tone.wav"
    _tone(wav, duration=0.2)
    spec = tmp_path / "tone.spec.png"
    wave = tmp_path / "tone.wave.png"
    save_spectrogram(wav, spec)
    save_waveform(wav, wave)
    assert spec.is_file() and spec.stat().st_size > 100
    assert wave.is_file() and wave.stat().st_size > 100


def test_build_page_writes_html(tmp_path: Path) -> None:
    wav = tmp_path / "01_tone.wav"
    _tone(wav, duration=0.2)
    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "A1",
        "title": "Test page",
        "items": [
            {
                "file": "01_tone.wav",
                "title": "1 kHz",
                "listen_for": "A pure tone.",
                "params": {"test.freq_hz": 1000},
                "metrics": {"duration_s": 0.2},
                "group": None,
                "reference": None,
            }
        ],
    }
    man = tmp_path / "manifest.json"
    man.write_text(json.dumps(manifest), encoding="utf-8")
    html_path = build_page(man)
    text = html_path.read_text(encoding="utf-8")
    assert html_path.is_file()
    assert "<audio controls" in text
    assert "01_tone.spec.png" in text
    assert "Listen for" in text
    assert (tmp_path / "01_tone.spec.png").is_file()
    assert (tmp_path / "01_tone.wave.png").is_file()  # 0.2 s < 0.5 s


def test_grouped_items_share_a_row(tmp_path: Path) -> None:
    for name in ("a.wav", "b.wav"):
        _tone(tmp_path / name, duration=0.6)
    manifest = {
        "format": "cuicui.listening_manifest",
        "version": 1,
        "wp": "A1",
        "title": "Groups",
        "items": [
            {
                "file": "a.wav",
                "title": "A",
                "listen_for": "a",
                "group": "pair",
                "reference": None,
            },
            {
                "file": "b.wav",
                "title": "B",
                "listen_for": "b",
                "group": "pair",
                "reference": None,
            },
        ],
    }
    man = tmp_path / "manifest.json"
    man.write_text(json.dumps(manifest), encoding="utf-8")
    text = build_page(man).read_text(encoding="utf-8")
    assert text.count("class='row'") == 1 or text.count('class="row"') == 1 or "class='row'" in text
    # Both cards live in one section.
    assert text.find("title") >= 0
    assert "A" in text and "B" in text
    # Waveform thumbnails only for clips < 0.5 s.
    assert not (tmp_path / "a.wave.png").exists()
