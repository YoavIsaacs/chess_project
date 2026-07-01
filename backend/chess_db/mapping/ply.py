"""Chess pair number + player -> monotonic ply. Pure; no I/O."""
from __future__ import annotations

from ..enums import Player


def ply_from_pair(move_number: int, player: Player) -> int:
    """Derive a true monotonic ply from the wire's pair number and mover.

    White's move in pair N is ply 2N-1; Black's is ply 2N. This resolves the
    spec's wording (the column means ply) against the wire (which sends the pair
    number, incrementing on White only). Recover the pair number for display
    with (ply + 1) // 2.
    """
    if move_number < 1:
        raise ValueError(f"move_number must be >= 1, got {move_number}")
    if player is Player.white:
        return 2 * move_number - 1
    return 2 * move_number
