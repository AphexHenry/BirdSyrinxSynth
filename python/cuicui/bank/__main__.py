"""Build a syllable bank from an annotated recording.

usage (from the repository root):
  PYTHONPATH=python python/.venv/bin/python -m cuicui.bank \\
      --wav "<dir>/22938.wav" --annotations "<dir>/22938.json" \\
      --name zf_22938 --species "zebra finch" --out assets/voicebanks/zf_22938/bank.json
"""

from __future__ import annotations

import argparse
from pathlib import Path

from cuicui.bank.build import build_bank, write_bank
from cuicui.bank.validate import validate_bank


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="python -m cuicui.bank", description=__doc__.splitlines()[0])
    ap.add_argument("--wav", type=Path, required=True)
    ap.add_argument("--annotations", type=Path, required=True, help="tseg annotation JSON")
    ap.add_argument("--name", required=True)
    ap.add_argument("--species", required=True)
    ap.add_argument("--description", default="")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args(argv)

    bank = build_bank(args.wav, args.annotations, name=args.name, species=args.species,
                      description=args.description)
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
          f"{args.out.stat().st_size / 1e6:.3f} MB → {args.out}")
    for p in bank["phrases"]:
        s = p["syllables"]
        dur = s[-1]["t_ms"] + len(s[-1]["f_hz"])
        print(f"  phrase {p['id']:2d} @ {p['start_s']:7.3f} s  {len(s):2d} syllables  {dur:5d} ms  "
              + " ".join(x["label"] for x in s))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
