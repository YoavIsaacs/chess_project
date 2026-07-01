"""WebSocket route test — connect/disconnect registration with the
ConnectionManager. Broadcast delivery itself is covered in
test_connection_manager.py; TestClient's websocket_connect runs on its own
thread/loop, so cross-loop broadcast timing isn't exercised here."""
from fastapi import FastAPI
from fastapi.testclient import TestClient

from api.connection_manager import ConnectionManager
from api.routers.websocket import build_websocket_router


def test_websocket_connect_and_disconnect_registers_with_manager():
    manager = ConnectionManager()
    app = FastAPI()
    app.include_router(build_websocket_router(manager))
    client = TestClient(app)

    with client.websocket_connect("/ws/live") as _ws:
        assert len(manager._connections) == 1

    assert len(manager._connections) == 0
