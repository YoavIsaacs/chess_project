"""Wire player token ('W'/'B') -> Player. Pure; no I/O."""
from __future__ import annotations

from ..enums import Player

_WIRE_TO_PLAYER = {
    "W": Player.white,
    "B": Player.black,
}


def player_from_wire(token: str) -> Player:
    """Map a MOVE packet player field to its Player. Raises ValueError on bad input."""
    try:
        return _WIRE_TO_PLAYER[token]
    except KeyError:
        raise ValueError(f"unknown player token: {token!r}") from None
