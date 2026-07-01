# test_serial_reader.py — Integration tests for uart_handler/serial_reader.py.
# Mocks: serial_asyncio.open_serial_connection, publisher functions, game_state.
# Critical: verifies ACK is sent before Redis publish on every packet type.

import asyncio
import pytest
import uuid
from unittest.mock import AsyncMock, MagicMock, patch

import uart_handler.game_state as game_state
import uart_handler.serial_reader as serial_reader


# ── Helpers ───────────────────────────────────────────────────────────────────

def make_serial_mock(*lines: str):
    """
    Return (reader_mock, writer_mock) where reader yields the given lines
    then stalls until the task is cancelled.

    readline() is implemented as a real async def so each call is genuinely
    awaited and returns bytes. The final call awaits asyncio.Event().wait()
    which blocks until the task is cancelled — no coroutine object is ever
    returned raw to the caller.
    """
    encoded = [line.encode("ascii") for line in lines]
    call_count = 0

    async def _readline():
        nonlocal call_count
        if call_count < len(encoded):
            result = encoded[call_count]
            call_count += 1
            return result
        # All lines consumed — stall until CancelledError propagates
        await asyncio.Event().wait()

    reader = AsyncMock()
    reader.readline = _readline

    writer = MagicMock()
    writer.write = MagicMock()
    writer.drain = AsyncMock()
    return reader, writer


async def run_serial_reader_for_n_lines(lines, redis_mock, n):
    """
    Run serial_reader.run() until n readline() calls have been consumed,
    then cancel the task.
    """
    reader, writer = make_serial_mock(*lines)
    writer_ref = [None]

    with patch("serial_asyncio.open_serial_connection", return_value=(reader, writer)):
        task = asyncio.create_task(serial_reader.run(writer_ref, redis_mock))
        for _ in range(n + 2):
            await asyncio.sleep(0)
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass

    return writer, writer_ref


# ── writer_ref population ─────────────────────────────────────────────────────

class TestWriterRef:

    @pytest.mark.asyncio
    async def test_writer_ref_populated_after_open(self):
        redis_mock = AsyncMock()
        _, writer_ref = await run_serial_reader_for_n_lines([], redis_mock, 0)
        assert writer_ref[0] is not None


# ── ACK-before-publish contract ───────────────────────────────────────────────

class TestAckBeforePublish:
    """
    The STM32 expects ACK within 200 ms. ACK must be written to serial
    before any await on Redis publish. Call order is verified via a shared list.
    """

    @pytest.mark.asyncio
    async def test_ack_before_publish_game_start(self):
        call_order = []
        redis_mock = AsyncMock()

        async def record_publish(channel, payload):
            call_order.append("publish")

        redis_mock.publish = AsyncMock(side_effect=record_publish)

        reader, writer = make_serial_mock(
            "GAME_START,Alice,Smith,Bob,Jones,600000,5000,1\n"
        )
        original_write = writer.write

        def record_write(data):
            if data.startswith(b"ACK"):
                call_order.append("ack")
            return original_write(data)

        writer.write = record_write
        writer_ref = [None]

        with patch("serial_asyncio.open_serial_connection", return_value=(reader, writer)):
            task = asyncio.create_task(serial_reader.run(writer_ref, redis_mock))
            await asyncio.sleep(0.05)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        assert call_order.index("ack") < call_order.index("publish"), (
            "ACK must be written before Redis publish"
        )

    @pytest.mark.asyncio
    async def test_ack_before_publish_move(self):
        call_order = []
        redis_mock = AsyncMock()

        async def record_publish(channel, payload):
            call_order.append("publish")

        redis_mock.publish = AsyncMock(side_effect=record_publish)

        game_state.game_id = "existing-game-id"
        game_state.active = True

        reader, writer = make_serial_mock("MOVE,1,W,295000,300000,3,22,55\n")
        original_write = writer.write

        def record_write(data):
            if data.startswith(b"ACK"):
                call_order.append("ack")
            return original_write(data)

        writer.write = record_write
        writer_ref = [None]

        with patch("serial_asyncio.open_serial_connection", return_value=(reader, writer)):
            task = asyncio.create_task(serial_reader.run(writer_ref, redis_mock))
            await asyncio.sleep(0.05)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        assert call_order.index("ack") < call_order.index("publish")

    @pytest.mark.asyncio
    async def test_ack_before_publish_game_end(self):
        call_order = []
        redis_mock = AsyncMock()

        async def record_publish(channel, payload):
            call_order.append("publish")

        redis_mock.publish = AsyncMock(side_effect=record_publish)

        game_state.game_id = "existing-game-id"
        game_state.active = True

        reader, writer = make_serial_mock("GAME_END,1-0\n")
        original_write = writer.write

        def record_write(data):
            if data.startswith(b"ACK"):
                call_order.append("ack")
            return original_write(data)

        writer.write = record_write
        writer_ref = [None]

        with patch("serial_asyncio.open_serial_connection", return_value=(reader, writer)):
            task = asyncio.create_task(serial_reader.run(writer_ref, redis_mock))
            await asyncio.sleep(0.05)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        assert call_order.index("ack") < call_order.index("publish")


# ── ACK content ───────────────────────────────────────────────────────────────

class TestAckContent:

    @pytest.mark.asyncio
    async def test_ack_game_start_format(self):
        redis_mock = AsyncMock()
        writer, _ = await run_serial_reader_for_n_lines(
            ["GAME_START,Alice,Smith,Bob,Jones,600000,5000,1\n"],
            redis_mock, 1
        )
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"ACK,GAME_START\n" in written

    @pytest.mark.asyncio
    async def test_ack_move_format(self):
        game_state.game_id = "some-id"
        game_state.active = True
        redis_mock = AsyncMock()
        writer, _ = await run_serial_reader_for_n_lines(
            ["MOVE,1,W,295000,300000,3,22,55\n"],
            redis_mock, 1
        )
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"ACK,MOVE\n" in written

    @pytest.mark.asyncio
    async def test_ack_game_end_format(self):
        game_state.game_id = "some-id"
        game_state.active = True
        redis_mock = AsyncMock()
        writer, _ = await run_serial_reader_for_n_lines(
            ["GAME_END,1-0\n"],
            redis_mock, 1
        )
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"ACK,GAME_END\n" in written


# ── GAME_START dispatch ───────────────────────────────────────────────────────

class TestGameStartDispatch:

    @pytest.mark.asyncio
    async def test_game_id_set_to_valid_uuid4(self):
        redis_mock = AsyncMock()
        await run_serial_reader_for_n_lines(
            ["GAME_START,Alice,Smith,Bob,Jones,600000,5000,1\n"],
            redis_mock, 1
        )
        assert game_state.game_id is not None
        parsed = uuid.UUID(game_state.game_id, version=4)
        assert str(parsed) == game_state.game_id

    @pytest.mark.asyncio
    async def test_active_set_true_on_game_start(self):
        redis_mock = AsyncMock()
        await run_serial_reader_for_n_lines(
            ["GAME_START,Alice,Smith,Bob,Jones,600000,5000,1\n"],
            redis_mock, 1
        )
        assert game_state.active is True

    @pytest.mark.asyncio
    async def test_publish_game_start_called_once(self):
        redis_mock = AsyncMock()
        with patch("uart_handler.publisher.publish_game_start", new_callable=AsyncMock) as mock_pub:
            await run_serial_reader_for_n_lines(
                ["GAME_START,Alice,Smith,Bob,Jones,600000,5000,1\n"],
                redis_mock, 1
            )
            assert mock_pub.call_count == 1

    @pytest.mark.asyncio
    async def test_successive_game_starts_get_different_game_ids(self):
        redis_mock = AsyncMock()
        await run_serial_reader_for_n_lines(
            ["GAME_START,Alice,Smith,Bob,Jones,600000,5000,1\n"],
            redis_mock, 1
        )
        first_id = game_state.game_id

        await run_serial_reader_for_n_lines(
            ["GAME_START,Carol,White,Dave,Black,300000,0,0\n"],
            redis_mock, 1
        )
        second_id = game_state.game_id

        assert first_id != second_id


# ── MOVE dispatch ─────────────────────────────────────────────────────────────

class TestMoveDispatch:

    @pytest.mark.asyncio
    async def test_move_with_active_game_publishes(self):
        game_state.game_id = "active-game-id"
        game_state.active = True
        redis_mock = AsyncMock()

        with patch("uart_handler.publisher.publish_move", new_callable=AsyncMock) as mock_pub:
            await run_serial_reader_for_n_lines(
                ["MOVE,1,W,295000,300000,3,22,55\n"],
                redis_mock, 1
            )
            assert mock_pub.call_count == 1

    @pytest.mark.asyncio
    async def test_move_with_no_active_game_is_ignored(self):
        # game_state defaults: game_id=None, active=False
        redis_mock = AsyncMock()

        with patch("uart_handler.publisher.publish_move", new_callable=AsyncMock) as mock_pub:
            await run_serial_reader_for_n_lines(
                ["MOVE,1,W,295000,300000,3,22,55\n"],
                redis_mock, 1
            )
            assert mock_pub.call_count == 0

    @pytest.mark.asyncio
    async def test_move_with_active_false_is_ignored(self):
        game_state.game_id = "some-id"
        game_state.active = False
        redis_mock = AsyncMock()

        with patch("uart_handler.publisher.publish_move", new_callable=AsyncMock) as mock_pub:
            await run_serial_reader_for_n_lines(
                ["MOVE,1,W,295000,300000,3,22,55\n"],
                redis_mock, 1
            )
            assert mock_pub.call_count == 0


# ── GAME_END dispatch ─────────────────────────────────────────────────────────

class TestGameEndDispatch:

    @pytest.mark.asyncio
    async def test_active_cleared_on_game_end(self):
        game_state.game_id = "ending-game"
        game_state.active = True
        redis_mock = AsyncMock()

        await run_serial_reader_for_n_lines(["GAME_END,1-0\n"], redis_mock, 1)
        assert game_state.active is False

    @pytest.mark.asyncio
    async def test_game_id_retained_after_game_end(self):
        game_state.game_id = "ending-game"
        game_state.active = True
        redis_mock = AsyncMock()

        await run_serial_reader_for_n_lines(["GAME_END,1-0\n"], redis_mock, 1)
        # game_id must NOT be cleared — redis_bridge may still drain in-flight EVALs
        assert game_state.game_id == "ending-game"

    @pytest.mark.asyncio
    async def test_game_end_with_no_active_game_is_ignored(self):
        redis_mock = AsyncMock()

        with patch("uart_handler.publisher.publish_game_end", new_callable=AsyncMock) as mock_pub:
            await run_serial_reader_for_n_lines(["GAME_END,1-0\n"], redis_mock, 1)
            assert mock_pub.call_count == 0

    @pytest.mark.asyncio
    async def test_publish_game_end_called_once(self):
        game_state.game_id = "ending-game"
        game_state.active = True
        redis_mock = AsyncMock()

        with patch("uart_handler.publisher.publish_game_end", new_callable=AsyncMock) as mock_pub:
            await run_serial_reader_for_n_lines(["GAME_END,0-1\n"], redis_mock, 1)
            assert mock_pub.call_count == 1


# ── Parse error handling ──────────────────────────────────────────────────────

class TestParseErrors:

    @pytest.mark.asyncio
    async def test_malformed_line_no_ack(self):
        redis_mock = AsyncMock()
        writer, _ = await run_serial_reader_for_n_lines(
            ["GARBAGE,not,a,valid,packet\n"], redis_mock, 1
        )
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"ACK" not in written

    @pytest.mark.asyncio
    async def test_malformed_line_no_publish(self):
        redis_mock = AsyncMock()
        await run_serial_reader_for_n_lines(
            ["GARBAGE,not,a,valid,packet\n"], redis_mock, 1
        )
        assert redis_mock.publish.call_count == 0

    @pytest.mark.asyncio
    async def test_malformed_line_does_not_crash_subsequent_valid_line(self):
        redis_mock = AsyncMock()
        await run_serial_reader_for_n_lines(
            [
                "GARBAGE,not,a,valid,packet\n",
                "GAME_START,Alice,Smith,Bob,Jones,600000,5000,1\n",
            ],
            redis_mock, 2
        )
        assert game_state.active is True

    @pytest.mark.asyncio
    async def test_empty_line_produces_no_ack(self):
        redis_mock = AsyncMock()
        writer, _ = await run_serial_reader_for_n_lines(["\n"], redis_mock, 1)
        written = b"".join(c.args[0] for c in writer.write.call_args_list)
        assert b"ACK" not in written
