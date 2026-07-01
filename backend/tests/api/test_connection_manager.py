from api.connection_manager import ConnectionManager


class FakeWebSocket:
    def __init__(self, fail_on_send: bool = False):
        self.accepted = False
        self.sent: list[str] = []
        self.fail_on_send = fail_on_send

    async def accept(self):
        self.accepted = True

    async def send_text(self, data: str):
        if self.fail_on_send:
            raise RuntimeError("connection closed")
        self.sent.append(data)


async def test_connect_accepts_and_registers():
    manager = ConnectionManager()
    ws = FakeWebSocket()

    await manager.connect(ws)

    assert ws.accepted
    assert ws in manager._connections


async def test_disconnect_removes_client():
    manager = ConnectionManager()
    ws = FakeWebSocket()
    await manager.connect(ws)

    await manager.disconnect(ws)

    assert ws not in manager._connections


async def test_broadcast_sends_to_all_clients():
    manager = ConnectionManager()
    ws1, ws2 = FakeWebSocket(), FakeWebSocket()
    await manager.connect(ws1)
    await manager.connect(ws2)

    await manager.broadcast({"type": "MOVE", "move_number": 1})

    assert len(ws1.sent) == 1
    assert len(ws2.sent) == 1
    assert '"type": "MOVE"' in ws1.sent[0]


async def test_broadcast_drops_failed_clients():
    manager = ConnectionManager()
    good, bad = FakeWebSocket(), FakeWebSocket(fail_on_send=True)
    await manager.connect(good)
    await manager.connect(bad)

    await manager.broadcast({"type": "PING"})

    assert bad not in manager._connections
    assert good in manager._connections
