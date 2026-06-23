# test_redis_bridge.py — Integration tests for uart_handler/redis_bridge.py.
# Mocks: redis.asyncio.from_url, serial writer.
# Exercises EVAL formatting, game lifecycle transitions, and error handling.

import asyncio
import json
import pytest
from unittest.mock import AsyncMock, MagicMock, patch

import uart_handler.game_state as game_state
import uart_handler.redis_bridge as redis_bridge


# ── Helpers ───────────────────────────────────────────────────────────────────

def make_writer_mock():
    writer = MagicMock()
    writer.write = MagicMock()
    return writer


def make_eval_message(data: dict) -> dict:
    """Wrap a dict as a Redis pub/sub message."""
    return {
        "type": "message",
        "data": json.dumps(data).encode("utf-8"),
    }


def make_pubsub_mock(*messages):
    """
    Return a mock pubsub that yields the given messages from listen(),
    then sets game_state.active = False so the bridge loop exits cleanly.
    """
    pubsub = AsyncMock()
    pubsub.subscribe = AsyncMock()
    pubsub.unsubscribe = AsyncMock()

    async def listen_gen():
        yield {"type": "subscribe", "data": 1}
        for msg in messages:
            yield msg
        game_state.active = False
        yield {"type": "message", "data": json.dumps({"_sentinel": True}).encode()}

    pubsub.listen = listen_gen
    return pubsub


async def run_bridge_with_messages(messages, writer=None, redis_url="redis://localhost"):
    """
    Set up an active game, run redis_bridge.run() with the given messages,
    and return the writer mock for assertion.
    """
    if writer is None:
        writer = make_writer_mock()

    writer_ref = [writer]
    game_state.game_id = "test-game-id"
    game_state.active = True

    pubsub = make_pubsub_mock(*messages)
    redis_client_mock = AsyncMock()
    redis_client_mock.pubsub = MagicMock(return_value=pubsub)
    redis_client_mock.aclose = AsyncMock()

    with patch("redis.asyncio.from_url", return_value=redis_client_mock):
        task = asyncio.create_task(redis_bridge.run(writer_ref, redis_url))
        await asyncio.sleep(0.1)
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass

    return writer


# ── EVAL wire format ──────────────────────────────────────────────────────────

class TestEvalWireFormat:

    @pytest.mark.asyncio
    async def test_positive_eval_has_plus_sign(self):
        msg = make_eval_message({"move": "Nf3", "eval_cp": 35, "quality": " !", "is_blunder": 0})
        writer = await run_bridge_with_messages([msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"EVAL,Nf3,+35," in written

    @pytest.mark.asyncio
    async def test_negative_eval_has_minus_sign(self):
        msg = make_eval_message({"move": "e4", "eval_cp": -180, "quality": "??", "is_blunder": 1})
        writer = await run_bridge_with_messages([msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"EVAL,e4,-180," in written

    @pytest.mark.asyncio
    async def test_zero_eval_has_plus_sign(self):
        msg = make_eval_message({"move": "d4", "eval_cp": 0, "quality": "  ", "is_blunder": 0})
        writer = await run_bridge_with_messages([msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"EVAL,d4,+0," in written

    @pytest.mark.asyncio
    async def test_forced_mate_white_sentinel(self):
        msg = make_eval_message({"move": "Qh7", "eval_cp": 32767, "quality": "!!", "is_blunder": 0})
        writer = await run_bridge_with_messages([msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"EVAL,Qh7,+32767," in written

    @pytest.mark.asyncio
    async def test_forced_mate_black_sentinel(self):
        msg = make_eval_message({"move": "Qh2", "eval_cp": -32767, "quality": "!!", "is_blunder": 0})
        writer = await run_bridge_with_messages([msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"EVAL,Qh2,-32767," in written

    @pytest.mark.asyncio
    async def test_quality_with_leading_space_preserved(self):
        """Quality tokens with a leading space (e.g. ' !', ' ?') must arrive intact."""
        msg = make_eval_message({"move": "Nf3", "eval_cp": 35, "quality": " !", "is_blunder": 0})
        writer = await run_bridge_with_messages([msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b" !," in written

    @pytest.mark.asyncio
    async def test_quality_double_space_preserved(self):
        """Blank quality token (two spaces) must be preserved exactly."""
        msg = make_eval_message({"move": "Nf3", "eval_cp": 10, "quality": "  ", "is_blunder": 0})
        writer = await run_bridge_with_messages([msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"  ," in written

    @pytest.mark.asyncio
    async def test_packet_ends_with_newline(self):
        msg = make_eval_message({"move": "Nf3", "eval_cp": 35, "quality": "!!", "is_blunder": 0})
        writer = await run_bridge_with_messages([msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        eval_start = written.index(b"EVAL,")
        newline_pos = written.index(b"\n", eval_start)
        assert written[newline_pos] == ord("\n")

    @pytest.mark.asyncio
    async def test_is_blunder_zero(self):
        msg = make_eval_message({"move": "e4", "eval_cp": 20, "quality": "!!", "is_blunder": 0})
        writer = await run_bridge_with_messages([msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert written.rstrip(b"\n").endswith(b",0")

    @pytest.mark.asyncio
    async def test_is_blunder_one(self):
        msg = make_eval_message({"move": "Bxh7", "eval_cp": -300, "quality": "??", "is_blunder": 1})
        writer = await run_bridge_with_messages([msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert written.rstrip(b"\n").endswith(b",1")


# ── Subscribe channel ─────────────────────────────────────────────────────────

class TestSubscribeChannel:

    @pytest.mark.asyncio
    async def test_subscribes_to_correct_eval_channel(self):
        game_state.game_id = "test-game-id"
        game_state.active = True

        writer = make_writer_mock()
        writer_ref = [writer]

        pubsub = make_pubsub_mock()
        redis_client_mock = AsyncMock()
        redis_client_mock.pubsub = MagicMock(return_value=pubsub)
        redis_client_mock.aclose = AsyncMock()

        with patch("redis.asyncio.from_url", return_value=redis_client_mock):
            task = asyncio.create_task(redis_bridge.run(writer_ref, "redis://localhost"))
            await asyncio.sleep(0.1)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        pubsub.subscribe.assert_called_once_with("game:test-game-id:eval")


# ── Game lifecycle ────────────────────────────────────────────────────────────

class TestGameLifecycle:

    @pytest.mark.asyncio
    async def test_no_redis_connection_when_no_active_game(self):
        """Bridge must not connect to Redis while game_state.active is False."""
        writer_ref = [make_writer_mock()]

        with patch("redis.asyncio.from_url") as mock_from_url:
            task = asyncio.create_task(redis_bridge.run(writer_ref, "redis://localhost"))
            await asyncio.sleep(0.05)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        mock_from_url.assert_not_called()

    @pytest.mark.asyncio
    async def test_no_redis_connection_when_writer_ref_none(self):
        """Bridge must wait for writer_ref to be populated before doing anything."""
        writer_ref = [None]
        game_state.game_id = "some-id"
        game_state.active = True

        with patch("redis.asyncio.from_url") as mock_from_url:
            task = asyncio.create_task(redis_bridge.run(writer_ref, "redis://localhost"))
            await asyncio.sleep(0.05)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        mock_from_url.assert_not_called()

    @pytest.mark.asyncio
    async def test_loop_exits_cleanly_when_active_set_false(self):
        """Setting active=False mid-listen must cause a clean exit with no exception."""
        msg = make_eval_message({"move": "e4", "eval_cp": 10, "quality": "  ", "is_blunder": 0})
        # run_bridge_with_messages sets active=False via the sentinel — just assert no raise
        await run_bridge_with_messages([msg])

    @pytest.mark.asyncio
    async def test_unsubscribes_after_game_ends(self):
        game_state.game_id = "ending-game"
        game_state.active = True

        writer_ref = [make_writer_mock()]
        pubsub = make_pubsub_mock()
        redis_client_mock = AsyncMock()
        redis_client_mock.pubsub = MagicMock(return_value=pubsub)
        redis_client_mock.aclose = AsyncMock()

        with patch("redis.asyncio.from_url", return_value=redis_client_mock):
            task = asyncio.create_task(redis_bridge.run(writer_ref, "redis://localhost"))
            await asyncio.sleep(0.1)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        pubsub.unsubscribe.assert_called()


# ── Error handling ────────────────────────────────────────────────────────────

class TestErrorHandling:

    @pytest.mark.asyncio
    async def test_malformed_json_produces_no_write(self):
        bad_msg = {"type": "message", "data": b"this is not json"}
        writer = await run_bridge_with_messages([bad_msg])
        writer.write.assert_not_called()

    @pytest.mark.asyncio
    async def test_missing_quality_key_produces_no_write(self):
        bad_msg = make_eval_message({"move": "e4", "eval_cp": 10, "is_blunder": 0})
        writer = await run_bridge_with_messages([bad_msg])
        writer.write.assert_not_called()

    @pytest.mark.asyncio
    async def test_missing_move_key_produces_no_write(self):
        bad_msg = make_eval_message({"eval_cp": 10, "quality": "!!", "is_blunder": 0})
        writer = await run_bridge_with_messages([bad_msg])
        writer.write.assert_not_called()

    @pytest.mark.asyncio
    async def test_none_data_produces_no_write(self):
        bad_msg = {"type": "message", "data": None}
        writer = await run_bridge_with_messages([bad_msg])
        writer.write.assert_not_called()

    @pytest.mark.asyncio
    async def test_subscribe_control_message_produces_no_write(self):
        """Subscribe/unsubscribe confirmations from Redis must not trigger a write."""
        control_msg = {"type": "subscribe", "data": 1}
        eval_msg = make_eval_message({"move": "Nf3", "eval_cp": 35, "quality": "!!", "is_blunder": 0})
        writer = await run_bridge_with_messages([control_msg, eval_msg])
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert written.count(b"EVAL,") == 1
