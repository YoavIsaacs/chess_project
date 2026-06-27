"""Centipawn integer -> pawn float for the `eval_score` column. Pure; no I/O."""
from __future__ import annotations

# Forced-mate sentinel on the wire (INT16_MAX). With the plain cp/100 conversion
# below it currently saturates to +/-327.67. Faithful mate representation in the
# DB is deferred to Step 16, when real Stockfish reports an exact mate distance
# rather than a sentinel (Step 13 decision 4).
MATE_SENTINEL_CP = 32767


def pawns_from_centipawns(eval_cp: int) -> float:
    """Convert signed centipawns to pawns (positive = white)."""
    return eval_cp / 100.0
