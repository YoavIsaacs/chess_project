"""Repository functions for the `moves` table (Step 13 Phase 4;
list_moves_for_game added in Step 14 to support the public move-history route)."""
from __future__ import annotations

import uuid

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from ..enums import Player
from ..mapping.timing import think_time_ms
from ..models.move import Move


async def insert_move(
    session: AsyncSession,
    game_id: uuid.UUID,
    move_number: int,
    move_notation: str,
    player: Player,
    clock_white_ms: int,
    clock_black_ms: int,
    increment_ms: int,
    eval_score: float | None,
    move_quality: str | None,
    is_blunder: bool,
    noise_level: int | None,
    temperature_c: float | None,
    humidity_pct: float | None,
) -> Move:
    """Insert a Move row, deriving `time_taken_ms` from this player's previous clock.

    Looks up the most recent Move for (game_id, player), ordered by
    move_number DESC. If found, `time_taken_ms` is computed via
    `think_time_ms(prev_clock, curr_clock, increment_ms)` using that
    player's own clock column (white -> clock_white_ms, black ->
    clock_black_ms). If this is the player's first move in the game,
    `time_taken_ms` is 0.
    """
    curr_clock_ms = clock_white_ms if player is Player.white else clock_black_ms

    prev_result = await session.execute(
        select(Move)
        .where(Move.game_id == game_id, Move.player == player)
        .order_by(Move.move_number.desc())
        .limit(1)
    )
    prev_move = prev_result.scalars().first()

    if prev_move is None:
        time_taken_ms = 0
    else:
        prev_clock_ms = (
            prev_move.clock_white_ms
            if player is Player.white
            else prev_move.clock_black_ms
        )
        time_taken_ms = think_time_ms(prev_clock_ms, curr_clock_ms, increment_ms)

    move = Move(
        game_id=game_id,
        move_number=move_number,
        move_notation=move_notation,
        player=player,
        time_taken_ms=time_taken_ms,
        clock_white_ms=clock_white_ms,
        clock_black_ms=clock_black_ms,
        eval_score=eval_score,
        move_quality=move_quality,
        is_blunder=is_blunder,
        noise_level=noise_level,
        temperature_c=temperature_c,
        humidity_pct=humidity_pct,
    )
    session.add(move)
    await session.flush()
    await session.refresh(move)
    return move


async def list_moves_for_game(
    session: AsyncSession, game_id: uuid.UUID
) -> list[Move]:
    """Return all moves for a game, ordered by move_number ascending — the
    move history feed for spec 4.2's Page 2 (stats) and Page 3 (analysis).
    """
    result = await session.execute(
        select(Move).where(Move.game_id == game_id).order_by(Move.move_number)
    )
    return list(result.scalars().all())
