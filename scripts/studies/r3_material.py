"""WP-R3 material: mark the new syllables automatically and build provisional banks (labels "?").

  22938: the human annotation (7.6–53.3 s) is kept as it is (same segment indices, so the TUNE1 note
         corrections still apply); syllables are marked automatically in the rest (53.3–187.6 s).
  22937: the whole recording is marked automatically (house finch, Macaulay 22937; the human's cleaned
         file, 108 s). Its phrases are blocks separated by 500 ms or more (the human).

Writes build/r3/ann/22938_full.json, build/r3/ann/22937_auto.json, build/r3/bank_22938_full.json,
build/r3/bank_22937.json, and checks that the first 12 phrases of the 22938 bank equal the factory
bank exactly.
usage: python/.venv/bin/python scripts/studies/r3_material.py
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))
from cuicui.bank import BuildSettings, build_bank, load_corrections, validate_bank, write_bank  # noqa: E402
from cuicui.bank.segment import annotate, write_annotation  # noqa: E402

DATA = Path("/Users/baptistebohelay/Dev/research/birds synth/data/annotations")
WORK = ROOT / "build/r3"
FACTORY = ROOT / "assets/voicebanks/zf_22938"


def main() -> None:
    a = annotate(DATA / "22938.wav", keep=json.loads((DATA / "22938.json").read_text()))
    write_annotation(a, WORK / "ann/22938_full.json")
    b = annotate(ROOT / "recordings/22937 - House Finch.wav")
    b["recording_id"] = "22937"
    write_annotation(b, WORK / "ann/22937_auto.json")

    full = build_bank(DATA / "22938.wav", WORK / "ann/22938_full.json", name="hf_22938_full",
                      species="house finch", created_by="WP-R3",
                      corrections=load_corrections(FACTORY / "note_corrections.json"))
    new = build_bank(ROOT / "recordings/22937 - House Finch.wav", WORK / "ann/22937_auto.json",
                     name="hf_22937", species="house finch", created_by="WP-R3",
                     settings=BuildSettings(phrase_gap_s=0.5))  # the human: blocks ≥ 500 ms apart
    for bank, path in ((full, WORK / "bank_22938_full.json"), (new, WORK / "bank_22937.json")):
        problems = validate_bank(bank)
        assert not problems, problems
        write_bank(bank, path)
        st = bank["stats"]
        print(f"{bank['name']}: {st['phrases']} phrases, {st['syllables']} syllables, "
              f"{st['hops']} hops → {path.relative_to(ROOT)}")
    factory = json.loads((FACTORY / "bank.json").read_text())
    n = len(factory["phrases"])
    assert full["phrases"][:n] == factory["phrases"], "the factory phrases changed"
    print(f"22938: the first {n} phrases equal the factory bank")


if __name__ == "__main__":
    main()
