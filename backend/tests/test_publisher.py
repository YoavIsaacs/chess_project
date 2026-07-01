# test_publisher.py — Unit tests for uart_handler/publisher.py.
# Redis client is mocked via AsyncMock — no real Redis connection needed.

import json
import pytest
from unittest.mock import AsyncMock

import uart_handler.publisher as publisher
from uart_handler.config import CHANNEL_GAME_EVENTS, CHANNEL_GAME_EVENTS_TEMPLATE


GAME_ID = "00000000-0000-0000-0000-000000000001"

GAME_START_PARSED = {
    "type": "GAME_START",
    "white_first": "Alice",
    "white_last": "Smith",
    "black_first": "Bob",
    "black_last": "Jones",
    "time_ms": 600000,
    "inc_ms": 5000,
    "eval_visible": 1,
}

MOVE_PARSED_WHITE = {
    "type": "MOVE",
    "move_num": 1,
    "player": "W",
    "white_ms": 295000,
    "black_ms": 300000,
    "mic_bar": 3,
    "temp_c": 22,
    "hum_pct": 55,
}

MOVE_PARSED_BLACK = {
    "type": "MOVE",
    "move_num": 2,
    "player": "B",
    "white_ms": 295000,
    "black_ms": 298000,
    "mic_bar": 5,
    "temp_c": 22,
    "hum_pct": 55,
}

GAME_END_PARSED = {
    "type": "GAME_END",
    "result": "1-0",
}


@pytest.fixture
def redis_mock():
    mock = AsyncMock()
    mock.publish = AsyncMock()
    return mock


# ── publish_game_start ────────────────────────────────────────────────────────

class TestPublishGameStart:

    @pytest.mark.asyncio
    async def test_publishes_to_game_events_channel(self, redis_mock):
        await publisher.publish_game_start(redis_mock, GAME_START_PARSED, GAME_ID)
        channel = redis_mock.publish.call_args[0][0]
        assert channel == CHANNEL_GAME_EVENTS

    @pytest.mark.asyncio
    async def test_publishes_exactly_once(self, redis_mock):
        await publisher.publish_game_start(redis_mock, GAME_START_PARSED, GAME_ID)
        assert redis_mock.publish.call_count == 1

    @pytest.mark.asyncio
    async def test_payload_type_is_game_start(self, redis_mock):
        await publisher.publish_game_start(redis_mock, GAME_START_PARSED, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["type"] == "GAME_START"

    @pytest.mark.asyncio
    async def test_payload_contains_game_id(self, redis_mock):
        await publisher.publish_game_start(redis_mock, GAME_START_PARSED, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["game_id"] == GAME_ID

    @pytest.mark.asyncio
    async def test_payload_player_fields(self, redis_mock):
        await publisher.publish_game_start(redis_mock, GAME_START_PARSED, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["white_first"] == "Alice"
        assert payload["white_last"] == "Smith"
        assert payload["black_first"] == "Bob"
        assert payload["black_last"] == "Jones"

    @pytest.mark.asyncio
    async def test_payload_time_fields(self, redis_mock):
        await publisher.publish_game_start(redis_mock, GAME_START_PARSED, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["time_ms"] == 600000
        assert payload["inc_ms"] == 5000

    @pytest.mark.asyncio
    async def test_payload_eval_visible_one(self, redis_mock):
        await publisher.publish_game_start(redis_mock, GAME_START_PARSED, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["eval_visible"] == 1

    @pytest.mark.asyncio
    async def test_payload_eval_visible_zero(self, redis_mock):
        parsed = {**GAME_START_PARSED, "eval_visible": 0}
        await publisher.publish_game_start(redis_mock, parsed, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["eval_visible"] == 0


# ── publish_move ──────────────────────────────────────────────────────────────

class TestPublishMove:

    @pytest.mark.asyncio
    async def test_publishes_to_per_game_channel(self, redis_mock):
        await publisher.publish_move(redis_mock, MOVE_PARSED_WHITE, GAME_ID)
        channel = redis_mock.publish.call_args[0][0]
        assert channel == CHANNEL_GAME_EVENTS_TEMPLATE.format(game_id=GAME_ID)

    @pytest.mark.asyncio
    async def test_publishes_exactly_once(self, redis_mock):
        await publisher.publish_move(redis_mock, MOVE_PARSED_WHITE, GAME_ID)
        assert redis_mock.publish.call_count == 1

    @pytest.mark.asyncio
    async def test_payload_type_is_move(self, redis_mock):
        await publisher.publish_move(redis_mock, MOVE_PARSED_WHITE, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["type"] == "MOVE"

    @pytest.mark.asyncio
    async def test_payload_contains_game_id(self, redis_mock):
        await publisher.publish_move(redis_mock, MOVE_PARSED_WHITE, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["game_id"] == GAME_ID

    @pytest.mark.asyncio
    async def test_payload_white_player(self, redis_mock):
        await publisher.publish_move(redis_mock, MOVE_PARSED_WHITE, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["player"] == "W"

    @pytest.mark.asyncio
    async def test_payload_black_player(self, redis_mock):
        await publisher.publish_move(redis_mock, MOVE_PARSED_BLACK, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["player"] == "B"

    @pytest.mark.asyncio
    async def test_payload_move_fields(self, redis_mock):
        await publisher.publish_move(redis_mock, MOVE_PARSED_WHITE, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["move_num"] == 1
        assert payload["white_ms"] == 295000
        assert payload["black_ms"] == 300000
        assert payload["mic_bar"] == 3
        assert payload["temp_c"] == 22
        assert payload["hum_pct"] == 55


# ── publish_game_end ──────────────────────────────────────────────────────────

class TestPublishGameEnd:

    @pytest.mark.asyncio
    async def test_publishes_to_per_game_channel(self, redis_mock):
        await publisher.publish_game_end(redis_mock, GAME_END_PARSED, GAME_ID)
        channel = redis_mock.publish.call_args[0][0]
        assert channel == CHANNEL_GAME_EVENTS_TEMPLATE.format(game_id=GAME_ID)

    @pytest.mark.asyncio
    async def test_publishes_exactly_once(self, redis_mock):
        await publisher.publish_game_end(redis_mock, GAME_END_PARSED, GAME_ID)
        assert redis_mock.publish.call_count == 1

    @pytest.mark.asyncio
    async def test_payload_type_is_game_end(self, redis_mock):
        await publisher.publish_game_end(redis_mock, GAME_END_PARSED, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["type"] == "GAME_END"

    @pytest.mark.asyncio
    async def test_payload_contains_game_id(self, redis_mock):
        await publisher.publish_game_end(redis_mock, GAME_END_PARSED, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["game_id"] == GAME_ID

    @pytest.mark.asyncio
    @pytest.mark.parametrize("result_token", ["1-0", "0-1", "1/2", "1-0T", "0-1T"])
    async def test_all_result_tokens_pass_through(self, redis_mock, result_token):
        parsed = {"type": "GAME_END", "result": result_token}
        await publisher.publish_game_end(redis_mock, parsed, GAME_ID)
        payload = json.loads(redis_mock.publish.call_args[0][1])
        assert payload["result"] == result_token
