"""Database fixtures for chess_db integration tests.

Isolation mechanism: each test gets a freshly created schema, dropped on
teardown (decoupled from Alembic per the Step 13 decision — fixtures build via
`Base.metadata.create_all`, not migrations). Per-test create/drop is used
instead of an outer-transaction-rollback so that everything stays inside one
function-scoped event loop; that sidesteps the cross-loop-scope fragility of a
session-scoped async engine under pytest-asyncio auto mode. NullPool ensures no
connection outlives the test that opened it.

Safety: the test engine connects to CHESS_DB_TEST_NAME (default 'chess_test'),
never the real CHESS_DB_NAME, so a stray test run cannot touch live data.
"""
import os

import pytest_asyncio
from sqlalchemy import URL
from sqlalchemy.ext.asyncio import AsyncSession, create_async_engine
from sqlalchemy.pool import NullPool

from chess_db import config
from chess_db.base import Base
from chess_db.models import Game, Move  # noqa: F401  (registers mappers / metadata)


def _test_url() -> URL:
    return URL.create(
        drivername=config.DB_DIALECT,
        username=config.DB_USER,
        password=config.DB_PASSWORD or None,
        host=config.DB_HOST,
        port=config.DB_PORT,
        database=os.environ.get("CHESS_DB_TEST_NAME", "chess_test"),
    )


@pytest_asyncio.fixture
async def engine():
    eng = create_async_engine(_test_url(), poolclass=NullPool)
    async with eng.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    try:
        yield eng
    finally:
        async with eng.begin() as conn:
            await conn.run_sync(Base.metadata.drop_all)
        await eng.dispose()


@pytest_asyncio.fixture
async def db_session(engine) -> AsyncSession:
    async with AsyncSession(engine, expire_on_commit=False) as session:
        yield session
