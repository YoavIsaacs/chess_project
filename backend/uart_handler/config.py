# config.py — UART Handler configuration constants

SERIAL_PORT = "/dev/serial0"
SERIAL_BAUD = 115200

REDIS_URL = "redis://localhost:6379"

# Static discovery channel (FastAPI always subscribed)
CHANNEL_GAME_EVENTS = "game_events"

# Per-game channel templates (format with game_id)
CHANNEL_GAME_EVENTS_TEMPLATE = "game:{game_id}:events"
CHANNEL_EVAL_TEMPLATE = "game:{game_id}:eval"
