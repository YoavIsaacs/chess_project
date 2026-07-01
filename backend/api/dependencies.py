"""Shared FastAPI dependencies: DB session + admin auth."""
from collections.abc import AsyncIterator

from fastapi import Header, HTTPException, status
from sqlalchemy.ext.asyncio import AsyncSession

from chess_db.session import get_session as _get_session

from .config import ADMIN_TOKEN


async def get_db_session() -> AsyncIterator[AsyncSession]:
    """FastAPI dependency wrapping chess_db's unit-of-work session scope."""
    async with _get_session() as session:
        yield session


async def require_admin(authorization: str | None = Header(default=None)) -> None:
    """Enforce `Authorization: Bearer <ADMIN_TOKEN>` on admin-only routes.

    401 if the header is missing or malformed, 403 if the token is wrong.
    Fails closed if ADMIN_TOKEN is unset — an empty token never matches, so
    a misconfigured deployment rejects every admin request instead of
    silently accepting one.
    """
    if authorization is None or not authorization.startswith("Bearer "):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Missing or malformed Authorization header",
            headers={"WWW-Authenticate": "Bearer"},
        )

    token = authorization.removeprefix("Bearer ").strip()
    if not ADMIN_TOKEN or token != ADMIN_TOKEN:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Invalid admin token",
        )
