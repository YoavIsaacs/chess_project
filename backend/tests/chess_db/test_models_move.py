"""Move model tests: structure (no DB) and behaviour (DB), including FK cascade."""
import uuid

import pytest
import sqlalchemy as sa

from chess_db.enums import Player
from chess_db.models import Game, Move


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


def _make_move(game_id, **overrides) -> Move:
    fields = dict(
        game_id=game_id,
        move_number=1,
        move_notation="e4",
        player=Player.white,
        time_taken_ms=3000,
        clock_white_ms=179000,
        clock_black_ms=180000,
    )
    fields.update(overrides)
    return Move(**fields)


# ----- structural (no DB) -----

def test_tablename():
    assert Move.__tablename__ == "moves"


def test_fk_to_games_with_cascade():
    fks = list(Move.__table__.columns["game_id"].foreign_keys)
    assert len(fks) == 1
    fk = fks[0]
    assert fk.column.table.name == "games"
    assert fk.column.name == "game_id"
    assert fk.ondelete == "CASCADE"


def test_composite_index_on_game_id_and_move_number():
    indexes = {idx.name: [c.name for c in idx.columns] for idx in Move.__table__.indexes}
    assert "ix_moves_game_id_move_number" in indexes
    assert indexes["ix_moves_game_id_move_number"] == ["game_id", "move_number"]


@pytest.mark.parametrize(
    "col", ["eval_score", "move_quality", "noise_level", "temperature_c", "humidity_pct"]
)
def test_optional_columns_are_nullable(col):
    assert Move.__table__.columns[col].nullable is True


@pytest.mark.parametrize(
    "col", ["move_number", "move_notation", "player", "time_taken_ms",
            "clock_white_ms", "clock_black_ms"]
)
def test_required_columns_are_not_null(col):
    assert Move.__table__.columns[col].nullable is False


def test_move_quality_is_varchar_2():
    assert Move.__table__.columns["move_quality"].type.length == 2


# ----- behavioural (DB) -----

async def test_insert_move_linked_to_game(db_session):
    game = _make_game()
    db_session.add(game)
    await db_session.flush()

    move = _make_move(game.game_id)
    db_session.add(move)
    await db_session.flush()
    await db_session.refresh(move)

    assert isinstance(move.move_id, uuid.UUID)  # locally generated default
    assert move.game_id == game.game_id
    assert move.is_blunder is False             # server default
    assert move.eval_score is None              # nullable, unset
    assert move.recorded_at is not None         # server default now()


async def test_player_enum_round_trips(db_session):
    game = _make_game()
    db_session.add(game)
    await db_session.flush()

    db_session.add(_make_move(game.game_id, move_number=2, player=Player.black, move_notation="e5"))
    await db_session.flush()
    db_session.expire_all()

    fetched = (await db_session.execute(sa.select(Move).where(Move.move_number == 2))).scalar_one()
    assert fetched.player is Player.black


async def test_optional_fields_persist_when_set(db_session):
    game = _make_game()
    db_session.add(game)
    await db_session.flush()

    db_session.add(_make_move(
        game.game_id,
        eval_score=-1.8, move_quality="?!", is_blunder=True,
        noise_level=512, temperature_c=22.5, humidity_pct=48.0,
    ))
    await db_session.flush()
    db_session.expire_all()

    m = (await db_session.execute(sa.select(Move))).scalar_one()
    assert m.eval_score == pytest.approx(-1.8)
    assert m.move_quality == "?!"
    assert m.is_blunder is True
    assert m.noise_level == 512
    assert m.temperature_c == pytest.approx(22.5)
    assert m.humidity_pct == pytest.approx(48.0)


async def test_fk_cascade_delete_removes_moves(db_session):
    game = _make_game()
    db_session.add(game)
    await db_session.flush()
    db_session.add_all([
        _make_move(game.game_id, move_number=1, move_notation="e4", player=Player.white),
        _make_move(game.game_id, move_number=2, move_notation="e5", player=Player.black),
    ])
    await db_session.flush()

    assert (await db_session.execute(sa.select(sa.func.count()).select_from(Move))).scalar_one() == 2

    await db_session.delete(game)
    await db_session.flush()

    remaining = (await db_session.execute(sa.select(sa.func.count()).select_from(Move))).scalar_one()
    assert remaining == 0
