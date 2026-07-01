"""API process configuration constants — env-driven, same pattern as
chess_db/config.py and uart_handler/config.py.
"""
import os

REDIS_URL = os.environ.get("CHESS_REDIS_URL", "redis://localhost:6379")

# Must stay in sync with uart_handler/config.py. The two processes don't
# share code (two-process design, spec 3.2), so these channel name strings
# are duplicated deliberately rather than imported across the process
# boundary.
CHANNEL_GAME_EVENTS = "game_events"
CHANNEL_GAME_EVENTS_TEMPLATE = "game:{game_id}:events"
CHANNEL_EVAL_TEMPLATE = "game:{game_id}:eval"

# Single shared admin credential (see checklist.md Step 14 design note).
# Set via the same conda activation hook pattern as the six DB env vars.
ADMIN_TOKEN = os.environ.get("ADMIN_TOKEN", "")

API_HOST = os.environ.get("CHESS_API_HOST", "0.0.0.0")
API_PORT = int(os.environ.get("CHESS_API_PORT", "8000"))
