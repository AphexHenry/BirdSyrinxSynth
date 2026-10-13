"""Syllable banks: a bird's real syllables for the whistle voice (WP-R1, `cuicui.syllable_bank`)."""

from cuicui.bank.build import (
    NOTE_CANDIDATES,
    NOTE_RULES,
    BuildSettings,
    anchors,
    apply_corrections,
    bank_hash,
    build_bank,
    load_corrections,
    note_candidates,
    write_bank,
)
from cuicui.bank.validate import load_bank, validate_bank

__all__ = [
    "NOTE_CANDIDATES",
    "NOTE_RULES",
    "BuildSettings",
    "anchors",
    "apply_corrections",
    "bank_hash",
    "build_bank",
    "load_corrections",
    "load_bank",
    "note_candidates",
    "validate_bank",
    "write_bank",
]
