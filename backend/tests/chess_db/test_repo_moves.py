"""Integration tests for chess_db.repositories.moves (Step 13 Phase 4)."""
from __future__ import annotations

import uuid

from chess_db.enums import Player
from chess_db.repositories.games import create_game
from chess_db.repositories.moves import insert_move


async def _make_game(db_session) -> uuid.UUID:
    game_id = uuid.uuid4()
    await create_game(
        db_session,
        game_id=game_id,
        white_first_name="John",
        white_last_name="Doe",
        black_first_name="Jane",
        black_last_name="Roe",
        time_control="Blitz 3+2",
        eval_visible=True,
    )
    return game_id


async def test_insert_move_first_white_move_zero_time_taken(db_session):
    game_id = await _make_game(db_session)
    move = await insert_move(
        db_session,
        game_id=game_id,
        move_number=1,
        move_notation="e4",
        player=Player.white,
        clock_white_ms=180000,
        clock_black_ms=180000,
        increment_ms=2000,
        eval_score=0.3,
        move_quality=None,
        is_blunder=False,
        noise_level=120,
        temperature_c=22.5,
        humidity_pct=40.0,
    )
    assert move.time_taken_ms == 0
    assert move.move_notation == "e4"
    assert move.player is Player.white
    assert move.clock_white_ms == 180000
    assert move.clock_black_ms == 180000


async def test_insert_move_first_black_move_zero_time_taken(db_session):
    game_id = await _make_game(db_session)
    move = await insert_move(
        db_session,
        game_id=game_id,
        move_number=1,
        move_notation="e5",
        player=Player.black,
        clock_white_ms=178500,
        clock_black_ms=180000,
        increment_ms=2000,
        eval_score=0.2,
        move_quality=None,
        is_blunder=False,
        noise_level=100,
        temperature_c=22.5,
        humidity_pct=40.0,
    )
    assert move.time_taken_ms == 0


async def test_insert_move_second_white_move_computes_time_taken(db_session):
    game_id = await _make_game(db_session)
    await insert_move(
        db_session,
        game_id=game_id,
        move_number=1,
        move_notation="e4",
        player=Player.white,
        clock_white_ms=180000,
        clock_black_ms=180000,
        increment_ms=2000,
        eval_score=0.3,
        move_quality=None,
        is_blunder=False,
        noise_level=120,
        temperature_c=22.5,
        humidity_pct=40.0,
    )
    move = await insert_move(
        db_session,
        game_id=game_id,
        move_number=2,
        move_notation="Nf3",
        player=Player.white,
        clock_white_ms=175000,
        clock_black_ms=177000,
        increment_ms=2000,
        eval_score=0.4,
        move_quality="!",
        is_blunder=False,
        noise_level=110,
        temperature_c=22.6,
        humidity_pct=40.1,
    )
    # prev clock_white_ms=180000, curr=175000, increment=2000 -> 7000
    assert move.time_taken_ms == 7000


async def test_insert_move_second_black_move_computes_time_taken(db_session):
    game_id = await _make_game(db_session)
    await insert_move(
        db_session,
        game_id=game_id,
        move_number=1,
        move_notation="e5",
        player=Player.black,
        clock_white_ms=178000,
        clock_black_ms=180000,
        increment_ms=1000,
        eval_score=0.1,
        move_quality=None,
        is_blunder=False,
        noise_level=90,
        temperature_c=21.0,
        humidity_pct=38.0,
    )
    move = await insert_move(
        db_session,
        game_id=game_id,
        move_number=2,
        move_notation="Nc6",
        player=Player.black,
        clock_white_ms=176000,
        clock_black_ms=174000,
        increment_ms=1000,
        eval_score=0.15,
        move_quality=None,
        is_blunder=False,
        noise_level=95,
        temperature_c=21.1,
        humidity_pct=38.2,
    )
    # prev clock_black_ms=180000, curr=174000, increment=1000 -> 7000
    assert move.time_taken_ms == 7000


async def test_insert_move_white_and_black_tracked_independently(db_session):
    game_id = await _make_game(db_session)
    await insert_move(
        db_session,
        game_id=game_id,
        move_number=1,
        move_notation="e4",
        player=Player.white,
        clock_white_ms=180000,
        clock_black_ms=180000,
        increment_ms=0,
        eval_score=None,
        move_quality=None,
        is_blunder=False,
        noise_level=None,
        temperature_c=None,
        humidity_pct=None,
    )
    # Black's first move must still be zero even though White has a prior row.
    move = await insert_move(
        db_session,
        game_id=game_id,
        move_number=1,
        move_notation="e5",
        player=Player.black,
        clock_white_ms=179000,
        clock_black_ms=180000,
        increment_ms=0,
        eval_score=None,
        move_quality=None,
        is_blunder=False,
        noise_level=None,
        temperature_c=None,
        humidity_pct=None,
    )
    assert move.time_taken_ms == 0


async def test_insert_move_nullable_fields_default_none(db_session):
    game_id = await _make_game(db_session)
    move = await insert_move(
        db_session,
        game_id=game_id,
        move_number=1,
        move_notation="e4",
        player=Player.white,
        clock_white_ms=180000,
        clock_black_ms=180000,
        increment_ms=0,
        eval_score=None,
        move_quality=None,
        is_blunder=False,
        noise_level=None,
        temperature_c=None,
        humidity_pct=None,
    )
    assert move.eval_score is None
    assert move.move_quality is None
    assert move.noise_level is None
    assert move.temperature_c is None
    assert move.humidity_pct is None


async def test_insert_move_is_blunder_flag(db_session):
    game_id = await _make_game(db_session)
    move = await insert_move(
        db_session,
        game_id=game_id,
        move_number=3,
        move_notation="Qxh7",
        player=Player.white,
        clock_white_ms=150000,
        clock_black_ms=150000,
        increment_ms=0,
        eval_score=-5.0,
        move_quality="??",
        is_blunder=True,
        noise_level=200,
        temperature_c=23.0,
        humidity_pct=45.0,
    )
    assert move.is_blunder is True
    assert move.move_quality == "??"


async def test_insert_move_assigns_move_id(db_session):
    game_id = await _make_game(db_session)
    move = await insert_move(
        db_session,
        game_id=game_id,
        move_number=1,
        move_notation="d4",
        player=Player.white,
        clock_white_ms=180000,
        clock_black_ms=180000,
        increment_ms=0,
        eval_score=None,
        move_quality=None,
        is_blunder=False,
        noise_level=None,
        temperature_c=None,
        humidity_pct=None,
    )
    assert isinstance(move.move_id, uuid.UUID)
