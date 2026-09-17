"""CLI: generate the source atlas, invert it, write assets + C++ header.

Run from the repo python/ directory (so cuicui_core is importable):

    uv run python -m cuicui.atlas --cache ../build/atlas_cache/C2 --out ../assets/atlas
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path

from cuicui.atlas.inverse import (
    build_inverse,
    load_atlas_bundle,
    plot_inverse,
    write_bundle,
    write_header,
)
from cuicui.atlas.source_atlas import (
    generate_atlas,
    plot_regime_map,
    save_forward_npz,
    write_onset_report,
)


def _root() -> Path:
    return Path(__file__).resolve().parents[3]


def main(argv: list[str] | None = None) -> None:
    root = _root()
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cache", type=Path, default=root / "build" / "atlas_cache" / "C2")
    p.add_argument("--out", type=Path, default=root / "assets" / "atlas")
    p.add_argument(
        "--header",
        type=Path,
        default=root / "core" / "include" / "cuicui" / "generated" / "atlas_tables.hpp",
    )
    p.add_argument("--figs", type=Path, default=root / "listening" / "C2" / "figs")
    p.add_argument("--workers", type=int, default=0)
    p.add_argument("--force", action="store_true")
    p.add_argument(
        "--skip-generate",
        action="store_true",
        help="Load cached per-β rows / forward npz and only invert + write.",
    )
    args = p.parse_args(argv)

    t0 = time.perf_counter()
    cache = args.cache
    forward_npz = cache / "forward.npz"
    if args.skip_generate and forward_npz.is_file():
        import numpy as np

        atlas = {k: v for k, v in np.load(forward_npz).items()}
        print(f"loaded forward atlas {forward_npz}", flush=True)
    else:
        atlas = generate_atlas(cache, workers=args.workers, force=args.force)
        save_forward_npz(atlas, forward_npz)

    elapsed_gen = float(atlas["elapsed_s"][0]) if "elapsed_s" in atlas else time.perf_counter() - t0
    print("building inverse tables", flush=True)
    inv = build_inverse(atlas)
    mono = inv["monotonicity"]
    print(
        f"monotonicity: {mono['violations']}/{mono['periodic_cells']} "
        f"({100.0 * mono['fraction']:.3f} %) dropped",
        flush=True,
    )
    print(
        f"smoothing α_lo max|Δ|={inv['smoothing']['alpha_lo']['max_abs']:.5f} "
        f"α_hi max|Δ|={inv['smoothing']['alpha_hi']['max_abs']:.5f}",
        flush=True,
    )
    meta = write_bundle(atlas, inv, args.out, root, elapsed_gen)
    print(f"wrote {args.out / 'source_atlas_v1.json'}  bin {meta['bin_bytes']} bytes", flush=True)
    meta2, arrays = load_atlas_bundle(args.out)
    write_header(meta2, arrays, args.header)
    print(f"wrote {args.header}  ({args.header.stat().st_size} bytes)", flush=True)

    figs = args.figs
    figs.mkdir(parents=True, exist_ok=True)
    plot_regime_map(atlas, figs / "regime_map.png")
    plot_inverse(inv, figs / "inverse_beta.png")
    onset = write_onset_report(atlas, figs / "onset_error.json")
    err = onset["max_abs_error"]
    print(f"onset vs analytic max|Δ|={err:.4f} pass={onset['pass_pm_0_01']}", flush=True)
    print(f"ATLAS OK in {time.perf_counter() - t0:.1f}s", flush=True)


if __name__ == "__main__":
    from cuicui.atlas.__main__ import main as _main

    _main()
