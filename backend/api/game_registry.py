"""In-memory registry of increment_ms per active game_id.

The MOVE wire event doesn't carry increment_ms (only GAME_START does), so
this holds each active game's increment between GAME_START and its later
MOVE events, for use in `insert_move`'s time_taken_ms derivation.

Keyed by game_id (str) rather than a single global — unlike uart_handler's
game_state.py, which only ever tracks one game because the STM32 side is
inherently single-game. This side is future-proofed for multiple
simultaneous games per spec 4.2's Live column note. Entries are removed on
GAME_END.
"""
_increments: dict[str, int] = {}


def set_increment(game_id: str, increment_ms: int) -> None:
    _increments[game_id] = increment_ms


def get_increment(game_id: str) -> int:
    return _increments.get(game_id, 0)


def clear_increment(game_id: str) -> None:
    _increments.pop(game_id, None)
