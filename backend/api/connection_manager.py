"""WebSocket connection registry + broadcast helper.

One instance lives on the FastAPI app for its process lifetime (created in
main.py, threaded into the websocket route and the redis_listener task).
"""
import asyncio
import json
import logging

from fastapi import WebSocket

log = logging.getLogger(__name__)


class ConnectionManager:
    def __init__(self) -> None:
        self._connections: set[WebSocket] = set()
        self._lock = asyncio.Lock()

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        async with self._lock:
            self._connections.add(websocket)

    async def disconnect(self, websocket: WebSocket) -> None:
        async with self._lock:
            self._connections.discard(websocket)

    async def broadcast(self, message: dict) -> None:
        """Send `message` (JSON-encoded) to every connected client.

        A client whose send fails (closed socket, network drop) is dropped
        from the registry rather than raising and aborting the broadcast to
        everyone else.
        """
        data = json.dumps(message)
        async with self._lock:
            targets = list(self._connections)

        for ws in targets:
            try:
                await ws.send_text(data)
            except Exception as exc:
                log.warning("WS send failed, dropping client: %s", exc)
                await self.disconnect(ws)
