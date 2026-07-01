# redis_bridge.py — Coroutine: subscribes to game:{game_id}:eval, writes EVAL to serial.
# Dynamically re-subscribes when game_id changes (new game started).

import asyncio
import json
import logging

import redis.asyncio as aioredis

from . import game_state
from .config import CHANNEL_EVAL_TEMPLATE

log = logging.getLogger(__name__)

# How often to poll for a new game_id when none is active yet (seconds)
_POLL_INTERVAL = 0.25


async def run(writer_ref: list, redis_url: str) -> None:
    """
    Wait for game_state.game_id to be set, subscribe to the eval channel,
    forward EVAL messages to serial, and repeat for each new game.
    writer_ref is a one-element list populated by serial_reader.run().
    """
    while True:
        # Wait until serial_reader has opened the port
        if writer_ref[0] is None:
            await asyncio.sleep(_POLL_INTERVAL)
            continue

        # Wait for an active game
        if game_state.game_id is None or not game_state.active:
            await asyncio.sleep(_POLL_INTERVAL)
            continue

        current_game_id = game_state.game_id
        channel = CHANNEL_EVAL_TEMPLATE.format(game_id=current_game_id)
        log.info("Subscribing to eval channel: %s", channel)

        # Create a dedicated pub/sub connection for this game
        redis_client = aioredis.from_url(redis_url)
        pubsub = redis_client.pubsub()
        await pubsub.subscribe(channel)

        try:
            async for message in pubsub.listen():
                # Stop if game ended or a new game started
                if not game_state.active or game_state.game_id != current_game_id:
                    log.info("Game ended or game_id changed — unsubscribing")
                    break

                if message["type"] != "message":
                    continue

                try:
                    data = json.loads(message["data"])
                except (json.JSONDecodeError, TypeError) as exc:
                    log.warning("EVAL JSON decode error: %s", exc)
                    continue

                _send_eval(writer_ref[0], data, current_game_id)

        except Exception as exc:
            log.error("redis_bridge error: %s", exc)
        finally:
            await pubsub.unsubscribe(channel)
            await redis_client.aclose()
            log.info("Unsubscribed from %s", channel)


def _send_eval(writer, data: dict, game_id: str) -> None:
    """
    Format and write an EVAL packet to serial.
    Wire format: EVAL,<move>,<eval_cp>,<quality>,<is_blunder>\n
    eval_cp is formatted with an explicit leading sign.
    quality is exactly 2 chars (leading space preserved).
    """
    try:
        move = data["move"]
        eval_cp = int(data["eval_cp"])
        quality = str(data["quality"])   # must be exactly 2 chars; preserve leading space
        is_blunder = int(data["is_blunder"])
    except (KeyError, ValueError) as exc:
        log.warning("Malformed EVAL payload (game %s): %s — %r", game_id, exc, data)
        return

    # Explicit sign on eval_cp (STM32 parser expects e.g. "+35", "-180", "+32767")
    sign = "+" if eval_cp >= 0 else ""
    packet = f"EVAL,{move},{sign}{eval_cp},{quality},{is_blunder}\n"
    writer.write(packet.encode("ascii"))
    log.info("TX EVAL: %r", packet.strip())
