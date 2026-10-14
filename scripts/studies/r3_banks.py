"""WP-R3: build the two house finch banks from the final (INGEST1-checked) annotations.

  hf_22938: 22938, the whole recording (the human annotation up to 53.3 s + the checked automatic
            syllables after it), rule E + the TUNE1 notes set by ear. Its first 12 phrases equal the
            factory bank zf_22938 (checked).
  hf_22937: 22937 (the human's cleaned file), phrases = blocks ≥ 500 ms apart (the human).
usage: python/.venv/bin/python scripts/studies/r3_banks.py   (after ingest1_analyze.py --write)
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))
from cuicui.bank import (  # noqa: E402
    BuildSettings,
    build_bank,
    load_corrections,
    validate_bank,
    write_bank,
)

DATA = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations")
VB = ROOT / "assets/voicebanks"
DESC = {
    "hf_22938": "House finch 22938 (Macaulay), the whole recording: 12 phrases annotated by hand (the "
                "factory bank zf_22938) + the rest marked automatically and checked by ear (INGEST1).",
    "hf_22937": "House finch 22937 (Macaulay), cleaned by the human (108 s): marked automatically and "
                "checked by ear (INGEST1). Phrases are blocks of song at least 500 ms apart.",
}


def main() -> None:
    banks = {
        "hf_22938": build_bank(DATA / "22938.wav", VB / "hf_22938/annotation.json",
                               name="hf_22938", species="house finch", description=DESC["hf_22938"],
                               created_by="WP-R3", settings=BuildSettings(variation=True),
                               corrections=load_corrections(VB / "zf_22938/note_corrections.json")),
        "hf_22937": build_bank(ROOT / "recordings/22937 - House Finch.wav",
                               VB / "hf_22937/annotation.json", name="hf_22937",
                               species="house finch", description=DESC["hf_22937"],
                               created_by="WP-R3",
                               settings=BuildSettings(phrase_gap_s=0.5, variation=True)),
    }
    for name, bank in banks.items():
        problems = validate_bank(bank)
        assert not problems, problems
        path = VB / name / "bank.json"
        write_bank(bank, path)
        st = bank["stats"]
        print(f"{name} {bank['hash']}: {st['phrases']} phrases, {st['syllables']} syllables, "
              f"{st['hops']} hops, {path.stat().st_size / 1e6:.2f} MB")
    factory = json.loads((VB / "zf_22938/bank.json").read_text())
    n = len(factory["phrases"])
    model_keys = ("type", "model", "scores")  # added by the shape model (D-036)
    plain = [[{k: v for k, v in s.items() if k not in model_keys} for s in p["syllables"]]
             for p in banks["hf_22938"]["phrases"][:n]]
    assert plain == [p["syllables"] for p in factory["phrases"]], "the factory phrases changed"
    print(f"hf_22938: its first {n} phrases equal zf_22938")


if __name__ == "__main__":
    main()
