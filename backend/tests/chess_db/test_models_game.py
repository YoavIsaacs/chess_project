"""Game model tests.

Structural tests inspect Game.__table__ and need no database. Behavioural tests
use the db_session fixture against the real test database.
"""
import uuid

import pytest
import sqlalchemy as sa

from chess_db.enums import GameResult
from chess_db.models import Game


def _make_game(**overrides) -> Game:
    fields = dict(
        game_id=uuid.uuid4(),
        white_first_name="Magnus",
        white_last_name="Carlsen",
        black_first_name="Hikaru",
        black_last_name="Nakamura",
        time_control="Blitz 3+2",
        eval_visible=True,
    )
    fields.update(overrides)
    return Game(**fields)


# ----- structural (no DB) -----

def test_tablename():
    assert Game.__tablename__ == "games"


def test_primary_key_is_game_id():
    pk = [c.name for c in Game.__table__.primary_key.columns]
    assert pk == ["game_id"]


@pytest.mark.parametrize(
    "col",
    ["white_first_name", "white_last_name", "black_first_name", "black_last_name"],
)
def test_name_columns_are_varchar_12_not_null(col):
    column = Game.__table__.columns[col]
    assert isinstance(column.type, sa.String)
    assert column.type.length == 12
    assert column.nullable is False


def test_time_control_varchar_20():
    column = Game.__table__.columns["time_control"]
    assert column.type.length == 20


def test_ended_at_is_nullable_started_at_is_not():
    assert Game.__table__.columns["ended_at"].nullable is True
    assert Game.__table__.columns["started_at"].nullable is False


# ----- behavioural (DB) -----

async def test_insert_and_read_back(db_session):
    game = _make_game()
    db_session.add(game)
    await db_session.flush()

    fetched = await db_session.get(Game, game.game_id)
    assert fetched is not None
    assert fetched.white_first_name == "Magnus"
    assert fetched.time_control == "Blitz 3+2"
    assert fetched.eval_visible is True


async def test_defaults_live_and_incomplete(db_session):
    game = _make_game()
    db_session.add(game)
    await db_session.flush()
    await db_session.refresh(game)

    assert game.is_live is True
    assert game.result is GameResult.incomplete
    assert game.ended_at is None
    assert game.started_at is not None  # server default now()


async def test_result_enum_round_trips(db_session):
    game = _make_game(result=GameResult.timeout_white, is_live=False)
    db_session.add(game)
    await db_session.flush()
    db_session.expunge_all()

    fetched = await db_session.get(Game, game.game_id)
    assert fetched.result is GameResult.timeout_white
    assert fetched.is_live is False


async def test_game_id_round_trips_as_uuid(db_session):
    gid = uuid.uuid4()
    db_session.add(_make_game(game_id=gid))
    await db_session.flush()
    db_session.expunge_all()

    fetched = await db_session.get(Game, gid)
    assert isinstance(fetched.game_id, uuid.UUID)
    assert fetched.game_id == gid
