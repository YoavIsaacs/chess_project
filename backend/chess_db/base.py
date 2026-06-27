"""Declarative base for all ORM models."""
from __future__ import annotations

from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    """Shared declarative base.

    `Base.metadata` is the single source the Alembic env and the test fixtures'
    `create_all` both read, which is what keeps migrations and models in sync.
    """
