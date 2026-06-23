# serial_reader.py — Coroutine: reads lines from serial, dispatches to parser/publisher.
# Sends ACK immediately on receipt (before any Redis publish) to honour the STM32 200 ms window.
# Orchestrates; owns no business logic.

import asyncio
import logging
import uuid

import serial_asyncio

import game_state
import publisher
from config import SERIAL_PORT, SERIAL_BAUD
from parser import parse_line

log = logging.getLogger(__name__)


async def run(writer_ref: list, redis_client) -> None:
    """
    Open the serial port and read lines indefinitely.
    writer_ref is a one-element list; populated here so redis_bridge can reach the writer.
    """
    reader, writer = await serial_asyncio.open_serial_connection(
        url=SERIAL_PORT, baudrate=SERIAL_BAUD
    )
    writer_ref[0] = writer
    log.info("Serial port %s open at %d baud", SERIAL_PORT, SERIAL_BAUD)

    while True:
        try:
            raw = await reader.readline()
        except Exception as exc:
            log.error("Serial read error: %s", exc)
            await asyncio.sleep(0.1)
            continue

        line = raw.decode("ascii", errors="replace").strip()
        if not line:
            continue

        log.debug("RX: %r", line)

        # Parse
        try:
            parsed = parse_line(line)
        except ValueError as exc:
            log.warning("Parse error (%s): %r", exc, line)
            continue

        ptype = parsed["type"]

        # ACK immediately — before any Redis publish
        ack_packet = f"ACK,{ptype}\n"
        writer.write(ack_packet.encode("ascii"))
        await writer.drain()
        log.info("TX ACK: %r", ack_packet.strip())

        # Dispatch
        if ptype == "GAME_START":
            game_state.game_id = str(uuid.uuid4())
            game_state.active = True
            log.info("New game_id: %s", game_state.game_id)
            await publisher.publish_game_start(redis_client, parsed, game_state.game_id)

        elif ptype == "MOVE":
            if not game_state.active or game_state.game_id is None:
                log.warning("MOVE received but no active game — ignoring")
                continue
            await publisher.publish_move(redis_client, parsed, game_state.game_id)

        elif ptype == "GAME_END":
            if not game_state.active or game_state.game_id is None:
                log.warning("GAME_END received but no active game — ignoring")
                continue
            await publisher.publish_game_end(redis_client, parsed, game_state.game_id)
            game_state.active = False
            # game_id intentionally retained so redis_bridge can drain any in-flight EVAL
