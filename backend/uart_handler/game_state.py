# game_state.py — Shared mutable state between serial_reader and redis_bridge coroutines.
# Both coroutines import this module directly. No other cross-module shared state exists.

game_id: str | None = None   # UUID4 string; None when no game is active
active: bool = False          # True from GAME_START until GAME_END is processed
