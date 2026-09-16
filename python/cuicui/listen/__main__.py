"""CLI: python -m cuicui.listen page <manifest.json>"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from cuicui.listen.page import build_page


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="cuicui.listen", description="Syrinx listening kit")
    sub = parser.add_subparsers(dest="cmd", required=True)
    page = sub.add_parser("page", help="build index.html next to a listening manifest")
    page.add_argument("manifest", type=Path, help="path to listening/<WP>/manifest.json")
    args = parser.parse_args(argv)
    if args.cmd == "page":
        out = build_page(args.manifest)
        print(f"wrote {out}")
        return 0
    parser.error(f"unknown command {args.cmd}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
