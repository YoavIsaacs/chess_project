"""redis_listener tests. get_session and repository calls are monkeypatched;
no real Redis or MySQL connection is ever opened."""
import json
import uuid
from contextlib import asynccontextmanager
from datetime import datetime

import pytest

from api import redis_listener
from api.connection_manager import ConnectionManager
from chess_db.enums import GameResult, Player


class DummySession:
    pass


@asynccontextmanager
async def _fake_get_session():
    yield DummySession()


@pytest.fixture(autouse=True)
def _patch_get_session(monkeypatch):
    monkeypatch.setattr(redis_listener, "get_session", _fake_get_session)


class RecordingManager(ConnectionManager):
    def __init__(self):
        super().__init__()
        self.broadcasts: list[dict] = []

    async def broadcast(self, message: dict) -> None:
        self.broadcasts.append(message)


# ---------------------------------------------------------------------------
# _handle_game_start / _handle_move / _handle_game_end
# ---------------------------------------------------------------------------


async def test_handle_game_start_creates_game_and_broadcasts(monkeypatch):
    created = {}

    async def fake_create_game(session, **kwargs):
        created.update(kwargs)

        class FakeGame:
            pass

        return FakeGame()

    monkeypatch.setattr(redis_listener.games_repo, "create_game", fake_create_game)
    manager = RecordingManager()
    game_id = str(uuid.uuid4())

    payload = {
        "type": "GAME_START",
        "game_id": game_id,
        "white_first": "Magnus",
        "white_last": "Carlsen",
        "black_first": "Hikaru",
        "black_last": "Nakamura",
        "time_ms": 180000,
        "inc_ms": 2000,
        "eval_visible": 1,
    }

    await redis_listener._handle_game_start(payload, manager)

    assert created["game_id"] == uuid.UUID(game_id)
    assert created["white_first_name"] == "Magnus"
    assert created["time_control"] == "3+2"
    assert created["eval_visible"] is True
    assert redis_listener.game_registry.get_increment(game_id) == 2000
    assert manager.broadcasts == [{"type": "GAME_START", "game_id": game_id}]

    redis_listener.game_registry.clear_increment(game_id)


async def test_handle_move_inserts_move_and_broadcasts(monkeypatch):
    inserted = {}
    eval_calls = []

    async def fake_insert_move(session, **kwargs):
        inserted.update(kwargs)

        class FakeMove:
            move_id = uuid.uuid4()

        return FakeMove()

    async def fake_request_eval(game_id, move):
        eval_calls.append((game_id, move))

    monkeypatch.setattr(redis_listener.moves_repo, "insert_move", fake_insert_move)
    monkeypatch.setattr(redis_listener, "request_eval", fake_request_eval)

    manager = RecordingManager()
    game_id = str(uuid.uuid4())
    redis_listener.game_registry.set_increment(game_id, 2000)

    # White's 2nd pair -> ply 3 (2*2-1). Deliberately != move_num so the test
    # can't pass by coincidence if the ply conversion silently regresses to a
    # pass-through of the wire's pair number.
    payload = {
        "type": "MOVE",
        "game_id": game_id,
        "move_num": 2,
        "player": "W",
        "white_ms": 178000,
        "black_ms": 180000,
        "mic_bar": 15,
        "temp_c": 22,
        "hum_pct": 40,
    }

    await redis_listener._handle_move(payload, manager)

    assert inserted["game_id"] == uuid.UUID(game_id)
    assert inserted["player"] is Player.white
    assert inserted["move_number"] == 3  # ply, not the wire's pair number (2)
    assert inserted["increment_ms"] == 2000
    assert inserted["clock_white_ms"] == 178000
    assert inserted["eval_score"] is None
    assert manager.broadcasts == [
        {"type": "MOVE", "game_id": game_id, "move_number": 3, "player": "W"}
    ]
    assert len(eval_calls) == 1

    redis_listener.game_registry.clear_increment(game_id)


async def test_handle_move_black_pair_maps_to_even_ply(monkeypatch):
    inserted = {}

    async def fake_insert_move(session, **kwargs):
        inserted.update(kwargs)

        class FakeMove:
            move_id = uuid.uuid4()

        return FakeMove()

    async def fake_request_eval(game_id, move):
        pass

    monkeypatch.setattr(redis_listener.moves_repo, "insert_move", fake_insert_move)
    monkeypatch.setattr(redis_listener, "request_eval", fake_request_eval)

    manager = RecordingManager()
    game_id = str(uuid.uuid4())

    # Black's 2nd pair -> ply 4 (2*2).
    payload = {
        "type": "MOVE",
        "game_id": game_id,
        "move_num": 2,
        "player": "B",
        "white_ms": 178000,
        "black_ms": 176000,
        "mic_bar": 10,
        "temp_c": 21,
        "hum_pct": 38,
    }

    await redis_listener._handle_move(payload, manager)

    assert inserted["player"] is Player.black
    assert inserted["move_number"] == 4


async def test_handle_game_end_ends_game_and_broadcasts(monkeypatch):
    ended = {}

    async def fake_end_game(session, game_id, result, ended_at):
        ended["game_id"] = game_id
        ended["result"] = result
        ended["ended_at"] = ended_at

        class FakeGame:
            pass

        return FakeGame()

    monkeypatch.setattr(redis_listener.games_repo, "end_game", fake_end_game)

    manager = RecordingManager()
    game_id = str(uuid.uuid4())
    redis_listener.game_registry.set_increment(game_id, 2000)

    payload = {"type": "GAME_END", "game_id": game_id, "result": "1-0"}

    await redis_listener._handle_game_end(payload, manager)

    assert ended["game_id"] == uuid.UUID(game_id)
    assert ended["result"] is GameResult.white_wins
    assert isinstance(ended["ended_at"], datetime)
    assert redis_listener.game_registry.get_increment(game_id) == 0
    assert manager.broadcasts == [
        {"type": "GAME_END", "game_id": game_id, "result": "white_wins"}
    ]


@pytest.mark.parametrize(
    "wire_token,expected_result",
    [
        ("1-0T", GameResult.timeout_white),
        ("0-1T", GameResult.timeout_black),
    ],
)
async def test_handle_game_end_timeout_result_mapping(
    monkeypatch, wire_token, expected_result
):
    """Regression guard for the corrected chess_db.mapping.result mapping:
    '1-0T' (White wins on time) -> timeout_white, not timeout_black, and
    vice versa for '0-1T'. An earlier stand-in mapping had this backwards."""
    captured = {}

    async def fake_end_game(session, game_id, result, ended_at):
        captured["result"] = result

        class FakeGame:
            pass

        return FakeGame()

    monkeypatch.setattr(redis_listener.games_repo, "end_game", fake_end_game)

    manager = RecordingManager()
    game_id = str(uuid.uuid4())

    await redis_listener._handle_game_end(
        {"type": "GAME_END", "game_id": game_id, "result": wire_token}, manager
    )

    assert captured["result"] is expected_result


# ---------------------------------------------------------------------------
# _handle_event dispatch
# ---------------------------------------------------------------------------


async def test_handle_event_dispatches_by_type(monkeypatch):
    calls = []

    async def fake_start(payload, manager):
        calls.append("start")

    async def fake_move(payload, manager):
        calls.append("move")

    async def fake_end(payload, manager):
        calls.append("end")

    monkeypatch.setattr(redis_listener, "_handle_game_start", fake_start)
    monkeypatch.setattr(redis_listener, "_handle_move", fake_move)
    monkeypatch.setattr(redis_listener, "_handle_game_end", fake_end)

    manager = RecordingManager()
    await redis_listener._handle_event({"type": "GAME_START"}, manager)
    await redis_listener._handle_event({"type": "MOVE"}, manager)
    await redis_listener._handle_event({"type": "GAME_END"}, manager)
    await redis_listener._handle_event({"type": "UNKNOWN"}, manager)

    assert calls == ["start", "move", "end"]


async def test_handle_event_swallows_handler_errors(monkeypatch):
    async def fake_start(payload, manager):
        raise RuntimeError("db is down")

    monkeypatch.setattr(redis_listener, "_handle_game_start", fake_start)
    manager = RecordingManager()

    # Must not raise — a single bad event shouldn't kill the listener loop.
    await redis_listener._handle_event({"type": "GAME_START"}, manager)


# ---------------------------------------------------------------------------
# run() — subscribe / listen / cleanup
# ---------------------------------------------------------------------------


def _make_fake_pubsub(messages):
    class FakePubSub:
        def __init__(self):
            self.subscribed = []
            self.psubscribed = []
            self.unsubscribed = []
            self.punsubscribed = []

        async def subscribe(self, channel):
            self.subscribed.append(channel)

        async def psubscribe(self, pattern):
            self.psubscribed.append(pattern)

        async def unsubscribe(self, channel):
            self.unsubscribed.append(channel)

        async def punsubscribe(self, pattern):
            self.punsubscribed.append(pattern)

        def listen(self):
            # Plain def returning an async generator instance (not a
            # coroutine) — the same pattern established for uart_handler's
            # redis_bridge tests: `async for` needs a generator, not an
            # AsyncMock or a bare coroutine.
            async def _gen():
                for m in messages:
                    yield m

            return _gen()

    return FakePubSub()


class FakeRedisClient:
    def __init__(self, pubsub):
        self._pubsub = pubsub

    def pubsub(self):
        return self._pubsub

    async def aclose(self):
        pass


async def test_run_processes_messages_and_cleans_up(monkeypatch):
    game_id = str(uuid.uuid4())
    messages = [
        {
            "type": "message",
            "channel": b"game_events",
            "data": json.dumps(
                {
                    "type": "GAME_START",
                    "game_id": game_id,
                    "white_first": "A",
                    "white_last": "B",
                    "black_first": "C",
                    "black_last": "D",
                    "time_ms": 180000,
                    "inc_ms": 0,
                    "eval_visible": 0,
                }
            ),
        },
        {"type": "subscribe", "channel": b"game_events", "data": 1},  # ignored
    ]
    fake_pubsub = _make_fake_pubsub(messages)
    fake_client = FakeRedisClient(fake_pubsub)
    monkeypatch.setattr(redis_listener.aioredis, "from_url", lambda url: fake_client)

    handled = []

    async def fake_handle_event(payload, manager):
        handled.append(payload)

    monkeypatch.setattr(redis_listener, "_handle_event", fake_handle_event)

    manager = RecordingManager()
    await redis_listener.run(manager, redis_url="redis://fake")

    assert len(handled) == 1
    assert handled[0]["game_id"] == game_id
    assert fake_pubsub.subscribed == ["game_events"]
    assert fake_pubsub.psubscribed == ["game:*:events"]
    assert fake_pubsub.unsubscribed == ["game_events"]
    assert fake_pubsub.punsubscribed == ["game:*:events"]


async def test_run_skips_malformed_json(monkeypatch):
    messages = [{"type": "message", "channel": b"game_events", "data": "not-json"}]
    fake_pubsub = _make_fake_pubsub(messages)
    fake_client = FakeRedisClient(fake_pubsub)
    monkeypatch.setattr(redis_listener.aioredis, "from_url", lambda url: fake_client)

    handled = []

    async def fake_handle_event(payload, manager):
        handled.append(payload)

    monkeypatch.setattr(redis_listener, "_handle_event", fake_handle_event)

    manager = RecordingManager()
    await redis_listener.run(manager, redis_url="redis://fake")

    assert handled == []
