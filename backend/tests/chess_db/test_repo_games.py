"""Integration tests for chess_db.repositories.games (Step 13 Phase 4)."""
from __future__ import annotations

import uuid
from datetime import datetime, timezone

import pytest

from chess_db.enums import GameResult
from chess_db.repositories.games import create_game, end_game, get_game


async def test_create_game_inserts_row(db_session):
    game_id = uuid.uuid4()
    game = await create_game(
        db_session,
        game_id=game_id,
        white_first_name="John",
        white_last_name="Doe",
        black_first_name="Jane",
        black_last_name="Roe",
        time_control="Blitz 3+2",
        eval_visible=True,
    )
    assert game.game_id == game_id
    assert game.white_first_name == "John"
    assert game.white_last_name == "Doe"
    assert game.black_first_name == "Jane"
    assert game.black_last_name == "Roe"
    assert game.time_control == "Blitz 3+2"
    assert game.eval_visible is True


async def test_create_game_server_defaults(db_session):
    game_id = uuid.uuid4()
    game = await create_game(
        db_session,
        game_id=game_id,
        white_first_name="John",
        white_last_name="Doe",
        black_first_name="Jane",
        black_last_name="Roe",
        time_control="Blitz 3+2",
        eval_visible=False,
    )
    assert game.is_live is True
    assert game.result is GameResult.incomplete
    assert game.started_at is not None
    assert game.ended_at is None


async def test_get_game_returns_row(db_session):
    game_id = uuid.uuid4()
    await create_game(
        db_session,
        game_id=game_id,
        white_first_name="A",
        white_last_name="B",
        black_first_name="C",
        black_last_name="D",
        time_control="Rapid 10+0",
        eval_visible=True,
    )
    fetched = await get_game(db_session, game_id)
    assert fetched is not None
    assert fetched.game_id == game_id


async def test_get_game_returns_none_when_missing(db_session):
    fetched = await get_game(db_session, uuid.uuid4())
    assert fetched is None


async def test_end_game_sets_result_and_ended_at(db_session):
    game_id = uuid.uuid4()
    await create_game(
        db_session,
        game_id=game_id,
        white_first_name="A",
        white_last_name="B",
        black_first_name="C",
        black_last_name="D",
        time_control="Classical 30+0",
        eval_visible=True,
    )
    ended_at = datetime.now(timezone.utc)
    game = await end_game(db_session, game_id, GameResult.white_wins, ended_at)
    assert game.is_live is False
    assert game.result is GameResult.white_wins
    assert game.ended_at is not None


async def test_end_game_timeout_result(db_session):
    game_id = uuid.uuid4()
    await create_game(
        db_session,
        game_id=game_id,
        white_first_name="A",
        white_last_name="B",
        black_first_name="C",
        black_last_name="D",
        time_control="Blitz 5+0",
        eval_visible=True,
    )
    game = await end_game(
        db_session, game_id, GameResult.timeout_black, datetime.now(timezone.utc)
    )
    assert game.result is GameResult.timeout_black
    assert game.is_live is False


async def test_end_game_draw_result(db_session):
    game_id = uuid.uuid4()
    await create_game(
        db_session,
        game_id=game_id,
        white_first_name="A",
        white_last_name="B",
        black_first_name="C",
        black_last_name="D",
        time_control="Blitz 5+0",
        eval_visible=True,
    )
    game = await end_game(db_session, game_id, GameResult.draw, datetime.now(timezone.utc))
    assert game.result is GameResult.draw


async def test_end_game_raises_for_missing_game(db_session):
    with pytest.raises(ValueError):
        await end_game(db_session, uuid.uuid4(), GameResult.draw, datetime.now(timezone.utc))
