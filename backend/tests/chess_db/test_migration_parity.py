"""Migration-parity test.

Asserts the Alembic migration produces a schema identical to `Base.metadata`
(no drift). Runs against CHESS_DB_TEST_NAME, never the real database. The
schema is built via `alembic upgrade head` — the one place in the test suite
the actual migration touches a database. Everywhere else (see
tests/chess_db/conftest.py), fixtures build schema via
`Base.metadata.create_all`, which stays decoupled from Alembic per the
Step 13 design; cleanup here follows the same decoupled pattern rather than
running the migration's own downgrade().
"""
from __future__ import annotations

import os
import asyncio
from pathlib import Path

import pytest
from alembic import command
from alembic.autogenerate import compare_metadata
from alembic.config import Config
from alembic.migration import MigrationContext
from sqlalchemy import URL, text
from sqlalchemy.ext.asyncio import create_async_engine
from sqlalchemy.pool import NullPool

from chess_db import config
from chess_db.base import Base
from chess_db.models import Game, Move  # noqa: F401  (registers mappers / metadata)

BACKEND_DIR = Path(__file__).resolve().parents[2]
ALEMBIC_INI = BACKEND_DIR / "alembic.ini"


def _test_db_name() -> str:
    return os.environ.get("CHESS_DB_TEST_NAME", "chess_test")


def _test_url() -> URL:
    return URL.create(
        drivername=config.DB_DIALECT,
        username=config.DB_USER,
        password=config.DB_PASSWORD or None,
        host=config.DB_HOST,
        port=config.DB_PORT,
        database=_test_db_name(),
    )


def _alembic_config() -> Config:
    cfg = Config(str(ALEMBIC_INI))
    cfg.set_main_option("script_location", str(BACKEND_DIR / "migrations"))
    return cfg


@pytest.fixture
def alembic_cfg(monkeypatch):
    """Point migrations/env.py at CHESS_DB_TEST_NAME for this test only.

    env.py builds its connection URL via chess_db.config.build_url(), which
    reads the module-level DB_NAME global at call time. Patching that global
    here redirects the migration — and only the migration — at the test
    database, leaving the real `chess` DB untouched.
    """
    monkeypatch.setattr(config, "DB_NAME", _test_db_name())
    return _alembic_config()


def _drop_everything_sync(connection) -> None:
    Base.metadata.drop_all(bind=connection)
    connection.execute(text("DROP TABLE IF EXISTS alembic_version"))


def test_migration_matches_models(alembic_cfg):
    # Plain `def`, not `async def`: command.upgrade() drives env.py's own
    # internal asyncio.run() call, which conflicts with the event loop
    # pytest-asyncio's auto mode would otherwise have us running inside.
    command.upgrade(alembic_cfg, "head")

    async def _diff_against_models():
        engine = create_async_engine(_test_url(), poolclass=NullPool)
        try:
            async with engine.connect() as conn:
                diff = await conn.run_sync(
                    lambda sync_conn: compare_metadata(
                        MigrationContext.configure(sync_conn), Base.metadata
                    )
                )
            return diff
        finally:
            async with engine.begin() as conn:
                await conn.run_sync(_drop_everything_sync)
            await engine.dispose()

    diff = asyncio.run(_diff_against_models())
    assert diff == [], f"Migration drift detected: {diff}"
