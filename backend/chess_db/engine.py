"""Async engine and session factory — one per process.

Importing this module constructs the engine, which imports the asyncmy DBAPI;
keep it out of import paths that must stay driver-free (e.g. the pure mapping
tests). No connection is opened until first use.
"""
from __future__ import annotations

from sqlalchemy.ext.asyncio import (
    AsyncSession,
    async_sessionmaker,
    create_async_engine,
)

from . import config

engine = create_async_engine(
    config.build_url(),
    echo=config.ECHO,
    pool_size=config.POOL_SIZE,
    max_overflow=config.POOL_MAX_OVERFLOW,
    pool_pre_ping=True,
)

SessionMaker = async_sessionmaker(
    bind=engine,
    class_=AsyncSession,
    expire_on_commit=False,
)


async def dispose() -> None:
    """Dispose the engine's connection pool. Call on process shutdown."""
    await engine.dispose()
