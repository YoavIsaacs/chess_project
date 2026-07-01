"""Unit-of-work helper: a session scope that commits on success, rolls back on error."""
from __future__ import annotations

from collections.abc import AsyncIterator
from contextlib import asynccontextmanager

from sqlalchemy.ext.asyncio import AsyncSession

from .engine import SessionMaker


@asynccontextmanager
async def get_session() -> AsyncIterator[AsyncSession]:
    """Yield a session bound to a single unit of work.

    Commits when the block exits cleanly, rolls back on any exception, always
    closes. Repository functions take a session argument rather than opening
    their own, so several can compose inside one transaction.
    """
    session = SessionMaker()
    try:
        yield session
        await session.commit()
    except Exception:
        await session.rollback()
        raise
    finally:
        await session.close()
