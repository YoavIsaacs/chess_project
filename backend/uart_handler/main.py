# main.py — UART Handler entry point.
# Starts the asyncio event loop and spawns the two coroutines:
#   serial_reader  — reads STM32 packets, sends ACK, publishes to Redis
#   redis_bridge   — subscribes to eval channel, writes EVAL packets to serial

import asyncio
import logging
import sys

import redis.asyncio as aioredis

from .config import REDIS_URL
from . import serial_reader
from . import redis_bridge

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s — %(message)s",
    stream=sys.stdout,
)
log = logging.getLogger("uart_handler.main")


async def main() -> None:
    log.info("UART Handler starting")

    # Shared Redis client for publishing (serial_reader + publisher)
    redis_client = aioredis.from_url(REDIS_URL)

    # One-element list so serial_reader can hand the writer to redis_bridge
    # after the serial port opens (avoids a circular dependency).
    writer_ref: list = [None]

    async with asyncio.TaskGroup() as tg:
        tg.create_task(
            serial_reader.run(writer_ref, redis_client),
            name="serial_reader",
        )
        tg.create_task(
            redis_bridge.run(writer_ref, REDIS_URL),
            name="redis_bridge",
        )


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        log.info("UART Handler stopped")
