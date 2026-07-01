"""Live game WebSocket — unauthenticated (public game viewing, per checklist.md's
Step 14 design note: browsers can't set custom headers on a WS handshake)."""
import logging

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from ..connection_manager import ConnectionManager

log = logging.getLogger(__name__)


def build_websocket_router(manager: ConnectionManager) -> APIRouter:
    """Bind the shared ConnectionManager into a fresh router instance.

    A factory rather than a module-level router, so the single
    ConnectionManager created in main.py is the one every connection
    actually registers with.
    """
    router = APIRouter()

    @router.websocket("/ws/live")
    async def live(websocket: WebSocket) -> None:
        await manager.connect(websocket)
        try:
            while True:
                # The client isn't expected to send anything; receive() is
                # just how Starlette detects a disconnect without a busy loop.
                await websocket.receive_text()
        except WebSocketDisconnect:
            pass
        finally:
            await manager.disconnect(websocket)

    return router
