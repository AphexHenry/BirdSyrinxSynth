#!/usr/bin/env python3
"""Generate core/include/cuicui/generated/atlas_tables.hpp from assets/atlas."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from cuicui.atlas.inverse import load_atlas_bundle, write_header  # noqa: E402


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--atlas-dir", type=Path, default=ROOT / "assets" / "atlas")
    p.add_argument(
        "--out",
        type=Path,
        default=ROOT / "core" / "include" / "cuicui" / "generated" / "atlas_tables.hpp",
    )
    args = p.parse_args(argv)
    meta, arrays = load_atlas_bundle(args.atlas_dir)
    write_header(meta, arrays, args.out)
    print(f"wrote {args.out} ({args.out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
