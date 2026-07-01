"""Repository functions for the `games` table (Step 13 Phase 4).

Callers supply the session and own the transaction boundary — these
functions never call commit() or rollback().
"""
from __future__ import annotations

import uuid
from datetime import datetime

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from ..enums import GameResult
from ..models.game import Game


async def create_game(
    session: AsyncSession,
    game_id: uuid.UUID,
    white_first_name: str,
    white_last_name: str,
    black_first_name: str,
    black_last_name: str,
    time_control: str,
    eval_visible: bool,
) -> Game:
    """Insert a new Game row.

    `is_live` and `result` are left unset so the DB's server defaults
    (`is_live=True`, `result='incomplete'`) apply — do not set them here.
    """
    game = Game(
        game_id=game_id,
        white_first_name=white_first_name,
        white_last_name=white_last_name,
        black_first_name=black_first_name,
        black_last_name=black_last_name,
        time_control=time_control,
        eval_visible=eval_visible,
    )
    session.add(game)
    await session.flush()
    await session.refresh(game)
    return game


async def get_game(session: AsyncSession, game_id: uuid.UUID) -> Game | None:
    """Return the Game row for `game_id`, or None if it does not exist."""
    result = await session.execute(select(Game).where(Game.game_id == game_id))
    return result.scalar_one_or_none()


async def end_game(
    session: AsyncSession,
    game_id: uuid.UUID,
    result: GameResult,
    ended_at: datetime,
) -> Game:
    """Flip a Game row to ended: is_live=False, ended_at stamped, result written.

    `result` must already be a `GameResult` (mapped by the caller from the
    wire token via `mapping/result.py`).

    Raises ValueError if no Game row exists for `game_id`.
    """
    game = await get_game(session, game_id)
    if game is None:
        raise ValueError(f"no game found for game_id: {game_id!r}")

    game.is_live = False
    game.ended_at = ended_at
    game.result = result

    await session.flush()
    await session.refresh(game)
    return game
