"""Public read-only game endpoints (spec 3.3 — game list, game detail, move history)."""
import uuid

from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy.ext.asyncio import AsyncSession

from chess_db.repositories import games as games_repo
from chess_db.repositories import moves as moves_repo

from ..dependencies import get_db_session
from ..schemas import GameOut, MoveOut

router = APIRouter(prefix="/games", tags=["games"])


@router.get("", response_model=list[GameOut])
async def list_games(
    limit: int = Query(default=50, ge=1, le=200),
    offset: int = Query(default=0, ge=0),
    session: AsyncSession = Depends(get_db_session),
) -> list[GameOut]:
    games = await games_repo.list_games(session, limit=limit, offset=offset)
    return [GameOut.model_validate(g) for g in games]


@router.get("/{game_id}", response_model=GameOut)
async def get_game(
    game_id: uuid.UUID,
    session: AsyncSession = Depends(get_db_session),
) -> GameOut:
    game = await games_repo.get_game(session, game_id)
    if game is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="game not found")
    return GameOut.model_validate(game)


@router.get("/{game_id}/moves", response_model=list[MoveOut])
async def get_moves(
    game_id: uuid.UUID,
    session: AsyncSession = Depends(get_db_session),
) -> list[MoveOut]:
    game = await games_repo.get_game(session, game_id)
    if game is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="game not found")

    moves = await moves_repo.list_moves_for_game(session, game_id)
    return [MoveOut.model_validate(m) for m in moves]
