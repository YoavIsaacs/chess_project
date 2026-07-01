"""Admin-only mutating endpoints. Every route here requires `require_admin`
(attached once at the router level, per checklist.md's Step 14 design note)."""
import uuid
from datetime import datetime, timezone

from fastapi import APIRouter, Body, Depends, HTTPException, status
from sqlalchemy.ext.asyncio import AsyncSession

from chess_db.enums import GameResult
from chess_db.repositories import games as games_repo

from ..dependencies import get_db_session, require_admin
from ..schemas import GameOut

router = APIRouter(
    prefix="/admin/games",
    tags=["admin"],
    dependencies=[Depends(require_admin)],
)


@router.delete("/{game_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_game(
    game_id: uuid.UUID,
    session: AsyncSession = Depends(get_db_session),
) -> None:
    deleted = await games_repo.delete_game(session, game_id)
    if not deleted:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="game not found")


@router.post("/{game_id}/force-end", response_model=GameOut)
async def force_end_game(
    game_id: uuid.UUID,
    result: GameResult = Body(embed=True),
    session: AsyncSession = Depends(get_db_session),
) -> GameOut:
    """Manually end a stuck live game (e.g. hardware disconnected mid-game).

    Body: {"result": "white_wins" | "black_wins" | "draw" | "timeout_white" | "timeout_black"}
    """
    try:
        game = await games_repo.end_game(
            session, game_id, result, datetime.now(timezone.utc)
        )
    except ValueError:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="game not found")
    return GameOut.model_validate(game)
