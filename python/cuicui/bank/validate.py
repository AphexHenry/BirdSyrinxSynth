"""Validation of `cuicui.syllable_bank` files: JSON schema plus checks a schema cannot express."""

from __future__ import annotations

import json
import math
from pathlib import Path

import jsonschema


def schema_path() -> Path:
    """`schemas/syllable_bank.schema.json` at the repository root."""
    return Path(__file__).resolve().parents[3] / "schemas" / "syllable_bank.schema.json"


def validate_bank(bank: dict) -> list[str]:
    """Return a list of problems (empty = valid). Raises nothing."""
    problems: list[str] = []
    schema = json.loads(schema_path().read_text())
    for err in jsonschema.Draft202012Validator(schema).iter_errors(bank):
        loc = "/".join(str(p) for p in err.absolute_path)
        problems.append(f"schema: {loc}: {err.message}")
    if problems:
        return problems
    for p in bank["phrases"]:
        prev_end = None
        for k, s in enumerate(p["syllables"]):
            where = f"phrase {p['id']} syllable {k}"
            if len(s["f_hz"]) != len(s["a"]):
                problems.append(f"{where}: f_hz and a differ in length")
            if not all(math.isfinite(v) for v in s["f_hz"] + s["a"]):
                problems.append(f"{where}: non-finite contour value")
            if prev_end is not None and s["t_ms"] < prev_end:
                problems.append(f"{where}: starts before the previous syllable ends")
            prev_end = s["t_ms"] + len(s["f_hz"])
            span = s["anchors"].get("rule_span_ms")
            if span is not None and not (0 <= span[0] < span[1] <= len(s["f_hz"])):
                problems.append(f"{where}: rule_span_ms outside the contour")
        if p["syllables"] and p["syllables"][0]["t_ms"] != 0:
            problems.append(f"phrase {p['id']}: first syllable t_ms must be 0")
    if [p["id"] for p in bank["phrases"]] != list(range(len(bank["phrases"]))):
        problems.append("phrase ids must be 0..n-1 in order")
    return problems


def load_bank(path: Path) -> dict:
    """Load and validate a bank file; raises ValueError listing every problem."""
    bank = json.loads(Path(path).read_text())
    problems = validate_bank(bank)
    if problems:
        raise ValueError(f"invalid syllable bank {path}:\n  " + "\n  ".join(problems))
    return bank
