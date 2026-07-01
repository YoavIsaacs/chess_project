"""Tests for the games repository additions: list_games, delete_game.

Assumes tests/chess_db/conftest.py exposes an async `db_session` fixture (per
the existing test_repo_games.py convention — the fixture name matches the
repository functions' first parameter). If your conftest uses a different
fixture name, rename the parameter in each test below to match.
"""
import uuid

from chess_db.repositories.games import create_game, delete_game, get_game, list_games
from chess_db.repositories.moves import insert_move, list_moves_for_game
from chess_db.enums import Player


async def _make_game(db_session, **overrides):
    defaults = dict(
        game_id=uuid.uuid4(),
        white_first_name="Magnus",
        white_last_name="Carlsen",
        black_first_name="Hikaru",
        black_last_name="Nakamura",
        time_control="3+2",
        eval_visible=True,
    )
    defaults.update(overrides)
    return await create_game(db_session, **defaults)


async def test_list_games_empty(db_session):
    assert await list_games(db_session) == []


async def test_list_games_returns_created_games(db_session):
    g1 = await _make_game(db_session)
    g2 = await _make_game(db_session, game_id=uuid.uuid4())

    games = await list_games(db_session)

    ids = {g.game_id for g in games}
    assert g1.game_id in ids
    assert g2.game_id in ids


async def test_list_games_orders_most_recent_first(db_session):
    await _make_game(db_session)
    await _make_game(db_session, game_id=uuid.uuid4())

    games = await list_games(db_session)

    started_ats = [g.started_at for g in games]
    assert started_ats == sorted(started_ats, reverse=True)


async def test_list_games_respects_limit_and_offset(db_session):
    for _ in range(3):
        await _make_game(db_session, game_id=uuid.uuid4())

    page1 = await list_games(db_session, limit=2, offset=0)
    page2 = await list_games(db_session, limit=2, offset=2)

    assert len(page1) == 2
    assert len(page2) == 1


async def test_delete_game_removes_row(db_session):
    game = await _make_game(db_session)

    deleted = await delete_game(db_session, game.game_id)

    assert deleted is True
    assert await get_game(db_session, game.game_id) is None


async def test_delete_game_returns_false_when_missing(db_session):
    deleted = await delete_game(db_session, uuid.uuid4())
    assert deleted is False


async def test_delete_game_cascades_to_moves(db_session):
    game = await _make_game(db_session)
    await insert_move(
        db_session,
        game_id=game.game_id,
        move_number=1,
        move_notation="e4",
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

    await delete_game(db_session, game.game_id)

    assert await list_moves_for_game(db_session, game.game_id) == []
