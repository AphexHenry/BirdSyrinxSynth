"""Build listening/<WP>/index.html from a validated manifest.json."""

from __future__ import annotations

import html
import json
import subprocess
from collections.abc import Iterable
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import soundfile as sf

from cuicui.listen.manifest import load_manifest
from cuicui.listen.spectrogram import save_spectrogram, save_waveform

CSS = """
:root { --bg:#111; --fg:#eee; --muted:#aaa; --card:#1c1c1c; --accent:#e8a54b; }
* { box-sizing: border-box; }
body {
  margin: 0;
  font-family: ui-sans-serif, system-ui, sans-serif;
  background: var(--bg);
  color: var(--fg);
}
header { padding: 1.5rem 1.5rem 0.5rem; }
header p { color: var(--muted); margin: 0.3rem 0; }
main { padding: 0 1rem 2rem; }
.row { display: flex; flex-wrap: wrap; gap: 1rem; margin-bottom: 1.5rem; }
.card {
  background: var(--card);
  border-radius: 10px;
  padding: 1rem;
  flex: 1 1 320px;
  min-width: 280px;
}
.card h2 { margin: 0 0 0.4rem; font-size: 1.05rem; }
.listen-for { color: var(--accent); margin: 0.4rem 0 0.8rem; }
audio { width: 100%; margin: 0.4rem 0; }
img { width: 100%; height: auto; border-radius: 6px; background: #000; }
table { width: 100%; border-collapse: collapse; font-size: 0.85rem; margin-top: 0.5rem; }
th { text-align: left; color: var(--muted); font-weight: 500; }
td, th { padding: 0.15rem 0.4rem; border-bottom: 1px solid #333; }
.ab { display: flex; gap: 0.75rem; }
.ab > div { flex: 1; }
.players { margin: 0.4rem 0 0.6rem; }
"""


def _git_hash(repo: Path) -> str:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=repo,
            check=True,
            capture_output=True,
            text=True,
        )
        h = out.stdout.strip()
        return h if h else "nogit"
    except (OSError, subprocess.CalledProcessError):
        return "nogit"


def _esc(value: Any) -> str:
    return html.escape("" if value is None else str(value))


def _kv_table(title: str, data: dict[str, Any] | None) -> str:
    if not data:
        return ""
    rows = "".join(
        f"<tr><th>{_esc(k)}</th><td>{_esc(v)}</td></tr>" for k, v in data.items()
    )
    return f"<table><caption>{_esc(title)}</caption>{rows}</table>"


def _duration_s(wav: Path) -> float:
    info = sf.info(str(wav))
    if info.samplerate <= 0:
        return 0.0
    return float(info.frames) / float(info.samplerate)


def _item_html(item: dict[str, Any], wav_rel: str, spec_rel: str, wave_rel: str | None) -> str:
    title = _esc(item.get("title", item["file"]))
    listen_for = _esc(item.get("listen_for", ""))
    ref = item.get("reference")
    players: list[str] = []
    if ref:
        players.append(
            "<div class='ab'>"
            f"<div><div>A: this</div><audio controls src='{_esc(wav_rel)}'></audio></div>"
            f"<div><div>B: reference</div><audio controls src='{_esc(ref)}'></audio></div>"
            "</div>"
        )
    else:
        players.append(f"<audio controls src='{_esc(wav_rel)}'></audio>")

    wave_img = f"<img src='{_esc(wave_rel)}' alt='waveform'>" if wave_rel else ""
    return (
        "<article class='card'>"
        f"<h2>{title}</h2>"
        f"<p class='listen-for'>Listen for: {listen_for}</p>"
        f"<div class='players'>{''.join(players)}</div>"
        f"<img src='{_esc(spec_rel)}' alt='spectrogram'>"
        f"{wave_img}"
        f"{_kv_table('params', item.get('params'))}"
        f"{_kv_table('metrics', item.get('metrics'))}"
        "</article>"
    )


def _grouped(items: Iterable[dict[str, Any]]) -> list[list[dict[str, Any]]]:
    """Keep first-seen order; items sharing a non-null group sit on the same row."""
    rows: list[list[dict[str, Any]]] = []
    index: dict[str, int] = {}
    for item in items:
        group = item.get("group")
        if group:
            if group in index:
                rows[index[group]].append(item)
            else:
                index[group] = len(rows)
                rows.append([item])
        else:
            rows.append([item])
    return rows


def build_page(manifest_path: Path | str) -> Path:
    manifest_path = Path(manifest_path).resolve()
    folder = manifest_path.parent
    data = load_manifest(manifest_path)
    repo = folder.parents[1] if folder.parent.name == "listening" else folder
    generated = datetime.now(UTC).strftime("%Y-%m-%d %H:%M UTC")
    git = _git_hash(repo)

    for item in data["items"]:
        wav = folder / item["file"]
        if not wav.is_file():
            raise FileNotFoundError(f"missing audio for listening item: {wav}")
        stem = wav.stem
        spec_png = folder / f"{stem}.spec.png"
        save_spectrogram(wav, spec_png)
        if _duration_s(wav) < 0.5:
            save_waveform(wav, folder / f"{stem}.wave.png")

    body_parts: list[str] = []
    for row in _grouped(data["items"]):
        cards = []
        for item in row:
            wav = folder / item["file"]
            stem = wav.stem
            wave_rel = f"{stem}.wave.png" if (folder / f"{stem}.wave.png").is_file() else None
            cards.append(_item_html(item, item["file"], f"{stem}.spec.png", wave_rel))
        body_parts.append(f"<section class='row'>{''.join(cards)}</section>")

    for fig in data.get("figures") or []:
        caption = _esc(fig.get("caption", ""))
        body_parts.append(
            "<section class='row'><article class='card'>"
            f"<h2>{_esc(fig.get('title', fig['file']))}</h2>"
            f"<p class='listen-for'>{caption}</p>"
            f"<img src='{_esc(fig['file'])}' alt='{_esc(fig.get('title', ''))}'>"
            "</article></section>"
        )

    page = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{_esc(data.get('title', data['wp']))}</title>
<style>{CSS}</style>
</head>
<body>
<header>
<h1>{_esc(data.get('title', data['wp']))}</h1>
<p>WP { _esc(data['wp']) } · generated {generated} · git { _esc(git) }</p>
</header>
<main>
{''.join(body_parts)}
</main>
</body>
</html>
"""
    out = folder / "index.html"
    out.write_text(page, encoding="utf-8")
    sidecar = folder / "page_meta.json"
    sidecar.write_text(
        json.dumps({"wp": data["wp"], "git": git, "generated_utc": generated}, indent=2),
        encoding="utf-8",
    )
    return out
