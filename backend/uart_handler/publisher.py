# publisher.py — Format and publish JSON messages to Redis.
# No serial knowledge. Takes a redis.asyncio client and a typed dict from parser.py.

import json
import logging

from config import (
    CHANNEL_GAME_EVENTS,
    CHANNEL_GAME_EVENTS_TEMPLATE,
)

log = logging.getLogger(__name__)


async def publish_game_start(redis_client, parsed: dict, game_id: str) -> None:
    """Publish GAME_START to the static game_events discovery channel."""
    payload = {
        "type": "GAME_START",
        "game_id": game_id,
        "white_first": parsed["white_first"],
        "white_last": parsed["white_last"],
        "black_first": parsed["black_first"],
        "black_last": parsed["black_last"],
        "time_ms": parsed["time_ms"],
        "inc_ms": parsed["inc_ms"],
        "eval_visible": parsed["eval_visible"],
    }
    await redis_client.publish(CHANNEL_GAME_EVENTS, json.dumps(payload))
    log.info("Published GAME_START to %s (game_id=%s)", CHANNEL_GAME_EVENTS, game_id)


async def publish_move(redis_client, parsed: dict, game_id: str) -> None:
    """Publish MOVE to the per-game events channel."""
    channel = CHANNEL_GAME_EVENTS_TEMPLATE.format(game_id=game_id)
    payload = {
        "type": "MOVE",
        "game_id": game_id,
        "move_num": parsed["move_num"],
        "player": parsed["player"],
        "white_ms": parsed["white_ms"],
        "black_ms": parsed["black_ms"],
        "mic_bar": parsed["mic_bar"],
        "temp_c": parsed["temp_c"],
        "hum_pct": parsed["hum_pct"],
    }
    await redis_client.publish(channel, json.dumps(payload))
    log.info("Published MOVE #%d to %s", parsed["move_num"], channel)


async def publish_game_end(redis_client, parsed: dict, game_id: str) -> None:
    """Publish GAME_END to the per-game events channel."""
    channel = CHANNEL_GAME_EVENTS_TEMPLATE.format(game_id=game_id)
    payload = {
        "type": "GAME_END",
        "game_id": game_id,
        "result": parsed["result"],
    }
    await redis_client.publish(channel, json.dumps(payload))
    log.info("Published GAME_END (%s) to %s", parsed["result"], channel)
