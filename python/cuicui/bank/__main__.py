"""Build a syllable bank from an annotated recording.

usage (from the repository root):
  PYTHONPATH=python python/.venv/bin/python -m cuicui.bank \\
      --wav "<dir>/22938.wav" --annotations "<dir>/22938.json" \\
      --name zf_22938 --species "zebra finch" \\
      --corrections assets/voicebanks/zf_22938/note_corrections.json \\
      --out assets/voicebanks/zf_22938/bank.json
--note-rule A rebuilds the R1 bank (D-019); E (the loudness-weighted mean) is the default (WP-R5).
"""

from __future__ import annotations

import argparse
from pathlib import Path

from cuicui.bank.build import (
    NOTE_CANDIDATES,
    BuildSettings,
    build_bank,
    load_corrections,
    write_bank,
)
from cuicui.bank.validate import validate_bank


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="python -m cuicui.bank", description=__doc__.splitlines()[0])
    ap.add_argument("--wav", type=Path, required=True)
    ap.add_argument("--annotations", type=Path, required=True, help="tseg annotation JSON")
    ap.add_argument("--name", required=True)
    ap.add_argument("--species", required=True)
    ap.add_argument("--description", default="")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--note-rule", choices=NOTE_CANDIDATES, default=BuildSettings().note_rule,
                    help="which MELODY2 rule sets a syllable's note (default E; A = the R1 bank)")
    ap.add_argument("--corrections", type=Path, help="notes set by ear (cuicui.note_corrections)")
    ap.add_argument("--created-by", default="WP-R1")
    args = ap.parse_args(argv)

    corrections = load_corrections(args.corrections) if args.corrections else None
    bank = build_bank(args.wav, args.annotations, name=args.name, species=args.species,
                      description=args.description, created_by=args.created_by,
                      settings=BuildSettings(note_rule=args.note_rule), corrections=corrections)
    problems = validate_bank(bank)
    if problems:
        print("INVALID BANK:\n  " + "\n  ".join(problems))
        return 1
    write_bank(bank, args.out)
    st = bank["stats"]
    centre = bank["bird_centre_cents"]
    print(f"bank {bank['name']} {bank['hash']}: {st['phrases']} phrases, "
          f"{st['syllables']} syllables, "
          f"{st['contour_ms'] / 1000:.1f} s of contours, centre {centre:.1f} c "
          f"({440 * 2 ** ((centre - 6900) / 1200):.0f} Hz), rule used the steady part for "
          f"{st['rule_used_steady']}/{st['syllables']}, {st['hops']} flagged hops, "
          f"{st.get('ear_notes', 0)} notes set by ear, "
          f"{st.get('no_note', 0)} without a clear note, "
          f"{args.out.stat().st_size / 1e6:.3f} MB → {args.out}")
    for p in bank["phrases"]:
        s = p["syllables"]
        dur = s[-1]["t_ms"] + len(s[-1]["f_hz"])
        print(f"  phrase {p['id']:2d} @ {p['start_s']:7.3f} s  {len(s):2d} syllables  {dur:5d} ms  "
              + " ".join(x["label"] for x in s))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
