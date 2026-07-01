"""Wire result token -> GameResult. Pure; no I/O."""
from __future__ import annotations

from ..enums import GameResult

_WIRE_TO_RESULT = {
    "1-0": GameResult.white_wins,
    "0-1": GameResult.black_wins,
    "1/2": GameResult.draw,
    "1-0T": GameResult.timeout_white,
    "0-1T": GameResult.timeout_black,
}


def result_from_wire(token: str) -> GameResult:
    """Map a GAME_END result token to its GameResult.

    `incomplete` is intentionally unreachable here: it is the default written at
    game creation, not a value sent on the wire. Raises ValueError on any
    unrecognised token (matching parser.py's strict, raise-on-bad-input style).
    """
    try:
        return _WIRE_TO_RESULT[token]
    except KeyError:
        raise ValueError(f"unknown result token: {token!r}") from None
