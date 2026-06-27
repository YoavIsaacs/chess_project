"""Domain enums backing the `result` and `player` columns (spec section 3.4)."""
from __future__ import annotations

import enum


class GameResult(enum.Enum):
    """`games.result`.

    `incomplete` is the default written at game creation and held for the whole
    live game; it is never carried on the wire (the result mapping only handles
    the five GAME_END tokens).
    """

    white_wins = "white_wins"
    black_wins = "black_wins"
    draw = "draw"
    timeout_white = "timeout_white"
    timeout_black = "timeout_black"
    incomplete = "incomplete"


class Player(enum.Enum):
    """`moves.player`."""

    white = "white"
    black = "black"
