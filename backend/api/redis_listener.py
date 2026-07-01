"""Background task: subscribes to the UART Handler's Redis channels and
persists game/move events to MySQL, then broadcasts them to WebSocket clients.

Mirrors uart_handler/redis_bridge.py's subscribe/listen pattern but on the
consuming side of the pipe: `game_events` (static, GAME_START discovery) and
`game:*:events` (pattern subscribe, MOVE/GAME_END for any active game)
rather than a single game's eval channel. Pattern-subscribing to all
per-game events channels — instead of dynamically subscribing to one
game_id at a time the way redis_bridge.py does for eval — avoids tracking
subscribe/unsubscribe state per game and supports multiple simultaneous
games (spec 4.2, Page 1 Live column) for free.

Player, result, and ply conversions use chess_db.mapping directly
(player.py, result.py, ply.py) — the wire sends a pair number that
increments on White only, and ply_from_pair() converts it to the true
monotonic ply the `moves.move_number` column stores.
"""
from __future__ import annotations

import asyncio
import json
import logging
import uuid
from datetime import datetime, timezone

import redis.asyncio as aioredis

from chess_db.mapping.player import player_from_wire
from chess_db.mapping.ply import ply_from_pair
from chess_db.mapping.result import result_from_wire
from chess_db.repositories import games as games_repo
from chess_db.repositories import moves as moves_repo
from chess_db.session import get_session

from . import game_registry, wire_mapping
from .analysis import request_eval
from .config import CHANNEL_GAME_EVENTS, REDIS_URL
from .connection_manager import ConnectionManager

log = logging.getLogger(__name__)

# YOLO (Step 15) is what actually determines move notation; the STM32 MOVE
# packet only carries move number, player, clocks, and sensor data. This
# placeholder is written at insert time and needs a Step 15 follow-up (a new
# update_move repository function) to fill in the real SAN notation.
_PENDING_NOTATION = "?"

# Reconnect backoff (run(), below): starts at 1s, doubles on each consecutive
# failed attempt, capped at 30s. Reset to the initial value any time a
# connection is established, so a long-lived connection dropping later
# doesn't inherit a stale, maxed-out backoff from an unrelated earlier outage.
_INITIAL_BACKOFF_S = 1.0
_MAX_BACKOFF_S = 30.0


async def run(manager: ConnectionManager, redis_url: str = REDIS_URL) -> None:
    """Connect, subscribe, and process messages — reconnecting with backoff
    if the connection is lost or can't be established, so a Redis restart
    doesn't require restarting the whole FastAPI process to recover.

    Runs until cancelled (main.py's lifespan cancels this task on shutdown).
    """
    backoff_s = _INITIAL_BACKOFF_S

    while True:
        try:
            await _run_once(manager, redis_url)
            # pubsub.listen() ending without an exception is unusual for a
            # real connection (normally it runs until cancelled or the
            # connection drops), but treat it the same as a drop: reconnect.
            log.warning("redis_listener: subscription ended, reconnecting")
        except asyncio.CancelledError:
            raise
        except Exception:
            log.exception(
                "redis_listener: connection error, retrying in %.1fs", backoff_s
            )
            await asyncio.sleep(backoff_s)
            backoff_s = min(backoff_s * 2, _MAX_BACKOFF_S)
        else:
            # Reached only after a connection was successfully established
            # (subscribe succeeded) and later ended cleanly — reset backoff
            # and retry immediately rather than penalizing a working setup.
            backoff_s = _INITIAL_BACKOFF_S


async def _run_once(manager: ConnectionManager, redis_url: str) -> None:
    """Single connect -> subscribe -> listen -> cleanup cycle.

    Raises on connection failure; run() is responsible for catching that and
    deciding whether/when to retry.
    """
    redis_client = aioredis.from_url(redis_url)
    pubsub = redis_client.pubsub()
    await pubsub.subscribe(CHANNEL_GAME_EVENTS)
    await pubsub.psubscribe("game:*:events")
    log.info("Subscribed to %s and game:*:events", CHANNEL_GAME_EVENTS)

    try:
        async for message in pubsub.listen():
            if message["type"] not in ("message", "pmessage"):
                continue

            try:
                payload = json.loads(message["data"])
            except (json.JSONDecodeError, TypeError) as exc:
                log.warning("Bad JSON on %s: %s", message.get("channel"), exc)
                continue

            await _handle_event(payload, manager)
    finally:
        await pubsub.unsubscribe(CHANNEL_GAME_EVENTS)
        await pubsub.punsubscribe("game:*:events")
        await redis_client.aclose()
        log.info("redis_listener connection closed")


async def _handle_event(payload: dict, manager: ConnectionManager) -> None:
    event_type = payload.get("type")
    try:
        if event_type == "GAME_START":
            await _handle_game_start(payload, manager)
        elif event_type == "MOVE":
            await _handle_move(payload, manager)
        elif event_type == "GAME_END":
            await _handle_game_end(payload, manager)
        else:
            log.warning("Unknown event type: %r", event_type)
    except Exception:
        # A single bad event must not kill the listener loop.
        log.exception("Error handling %s event: %r", event_type, payload)


async def _handle_game_start(payload: dict, manager: ConnectionManager) -> None:
    game_id = payload["game_id"]
    game_registry.set_increment(game_id, payload["inc_ms"])

    async with get_session() as session:
        await games_repo.create_game(
            session,
            game_id=uuid.UUID(game_id),
            white_first_name=payload["white_first"],
            white_last_name=payload["white_last"],
            black_first_name=payload["black_first"],
            black_last_name=payload["black_last"],
            time_control=wire_mapping.format_time_control(
                payload["time_ms"], payload["inc_ms"]
            ),
            eval_visible=bool(payload["eval_visible"]),
        )

    log.info("Created game %s", game_id)
    await manager.broadcast({"type": "GAME_START", "game_id": game_id})


async def _handle_move(payload: dict, manager: ConnectionManager) -> None:
    game_id = payload["game_id"]
    player = player_from_wire(payload["player"])
    ply = ply_from_pair(payload["move_num"], player)
    increment_ms = game_registry.get_increment(game_id)

    async with get_session() as session:
        move = await moves_repo.insert_move(
            session,
            game_id=uuid.UUID(game_id),
            move_number=ply,
            move_notation=_PENDING_NOTATION,
            player=player,
            clock_white_ms=payload["white_ms"],
            clock_black_ms=payload["black_ms"],
            increment_ms=increment_ms,
            eval_score=None,
            move_quality=None,
            is_blunder=False,
            noise_level=payload["mic_bar"],
            temperature_c=float(payload["temp_c"]),
            humidity_pct=float(payload["hum_pct"]),
        )

    log.info("Inserted ply %d (pair %d) for game %s", ply, payload["move_num"], game_id)
    await manager.broadcast(
        {
            "type": "MOVE",
            "game_id": game_id,
            "move_number": ply,
            "player": payload["player"],
        }
    )
    await request_eval(game_id, move)


async def _handle_game_end(payload: dict, manager: ConnectionManager) -> None:
    game_id = payload["game_id"]
    result = result_from_wire(payload["result"])

    async with get_session() as session:
        await games_repo.end_game(
            session,
            game_id=uuid.UUID(game_id),
            result=result,
            ended_at=datetime.now(timezone.utc),
        )

    game_registry.clear_increment(game_id)
    log.info("Ended game %s (%s)", game_id, result.value)
    await manager.broadcast(
        {"type": "GAME_END", "game_id": game_id, "result": result.value}
    )
