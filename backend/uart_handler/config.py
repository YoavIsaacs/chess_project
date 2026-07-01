# config.py — UART Handler configuration constants
#
# Env-driven, same pattern as chess_db/config.py and api/config.py: every
# tunable has a sane local default but can be overridden via the environment
# without touching source. REDIS_URL in particular must stay in sync with
# api/config.py's CHESS_REDIS_URL — both processes talk to the same Redis
# instance, so this is the single env var that controls that for this side.

import os

SERIAL_PORT = os.environ.get("CHESS_SERIAL_PORT", "/dev/serial0")
SERIAL_BAUD = int(os.environ.get("CHESS_SERIAL_BAUD", "115200"))

REDIS_URL = os.environ.get("CHESS_REDIS_URL", "redis://localhost:6379")

# Static discovery channel (FastAPI always subscribed)
CHANNEL_GAME_EVENTS = "game_events"

# Per-game channel templates (format with game_id)
CHANNEL_GAME_EVENTS_TEMPLATE = "game:{game_id}:events"
CHANNEL_EVAL_TEMPLATE = "game:{game_id}:eval"
