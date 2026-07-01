"""Tests for the moves repository addition: list_moves_for_game.

Assumes tests/chess_db/conftest.py exposes an async `db_session` fixture (see
the note in test_repo_games_list_delete.py).
"""
import uuid

from chess_db.enums import Player
from chess_db.repositories.games import create_game
from chess_db.repositories.moves import insert_move, list_moves_for_game


async def _make_game(db_session):
    return await create_game(
        db_session,
        game_id=uuid.uuid4(),
        white_first_name="Magnus",
        white_last_name="Carlsen",
        black_first_name="Hikaru",
        black_last_name="Nakamura",
        time_control="3+2",
        eval_visible=True,
    )


async def _make_move(db_session, game_id, move_number, notation):
    await insert_move(
        db_session,
        game_id=game_id,
        move_number=move_number,
        move_notation=notation,
        player=Player.white,
        clock_white_ms=178000,
        clock_black_ms=180000,
        increment_ms=0,
        eval_score=None,
        move_quality=None,
        is_blunder=False,
        noise_level=None,
        temperature_c=None,
        humidity_pct=None,
    )


async def test_list_moves_for_game_empty(db_session):
    game = await _make_game(db_session)
    assert await list_moves_for_game(db_session, game.game_id) == []


async def test_list_moves_for_game_orders_by_move_number(db_session):
    game = await _make_game(db_session)
    await _make_move(db_session, game.game_id, 2, "Nf3")
    await _make_move(db_session, game.game_id, 1, "e4")

    moves = await list_moves_for_game(db_session, game.game_id)

    assert [m.move_number for m in moves] == [1, 2]


async def test_list_moves_for_game_only_returns_that_games_moves(db_session):
    game1 = await _make_game(db_session)
    game2 = await _make_game(db_session)
    await _make_move(db_session, game1.game_id, 1, "e4")
    await _make_move(db_session, game2.game_id, 1, "d4")

    moves = await list_moves_for_game(db_session, game1.game_id)

    assert len(moves) == 1
    assert moves[0].move_notation == "e4"
