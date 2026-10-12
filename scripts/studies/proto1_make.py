"""PROTO1 — playable web prototype of playing modes A + B with real syllables.

Builds listening/PROTO1/index.html (self-contained) from the committed syllable bank
`assets/voicebanks/zf_22938/bank.json` — the same file the plugin plays (WP-R1). Every voiced syllable
of 22938, grouped into the bird's real phrases, with per-syllable note anchors for each pitch rule.
Audio is synthesised in the page (whistle, or whistle + colour); see scripts/studies/proto1_page.html.

The bank recipe moved to `python/cuicui/bank` in WP-R1 (unchanged; the rebuilt bank is identical to
the R0 one). `--rebuild` rebuilds the bank from the recording first.

usage: python/.venv/bin/python scripts/studies/proto1_make.py [--rebuild]
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))
from cuicui.bank import build_bank, load_bank, write_bank  # noqa: E402

HERE = Path(__file__).resolve().parent
OUT = ROOT / "listening/PROTO1"
REC = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations/22938.wav")
BANK = ROOT / "assets/voicebanks/zf_22938/bank.json"


def main():
    if "--rebuild" in sys.argv[1:]:
        bank = build_bank(REC, REC.with_suffix(".json"), name="zf_22938", species="zebra finch",
                          description=json.loads(BANK.read_text())["description"] if BANK.exists() else "")
        write_bank(bank, BANK)
    bank = load_bank(BANK)
    phrases = bank["phrases"]

    OUT.mkdir(parents=True, exist_ok=True)
    html = (HERE / "proto1_page.html").read_text()
    html = html.replace("<!--BANK-->", "<script>window.BANK = " + json.dumps(bank, separators=(",", ":")) + ";</script>")
    (OUT / "index.html").write_text(html)

    n_syl = sum(len(p["syllables"]) for p in phrases)
    print(f"bank {bank['hash']}: {len(phrases)} phrases, {n_syl} syllables, bird centre "
          f"{bank['bird_centre_cents']:.0f} c ({440 * 2 ** ((bank['bird_centre_cents'] - 6900) / 1200):.0f} Hz), "
          f"page {len(html) / 1e6:.2f} MB")
    for p in phrases:
        labels = " ".join(s["label"] for s in p["syllables"])
        dur = p["syllables"][-1]["t_ms"] + len(p["syllables"][-1]["f_hz"])
        print(f"  phrase {p['id']:2d} @ {p['start_s']:6.2f}s  {len(p['syllables']):2d} syll  {dur:5d} ms  {labels}")


if __name__ == "__main__":
    main()
