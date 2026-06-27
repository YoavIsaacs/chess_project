"""Reusable column factories for the shared timestamp semantics in section 3.4.

Not class mixins in the inheritance sense — these return configured
`mapped_column` descriptors so both models share one definition of an
insert-stamped timestamp and of a nullable timestamp, without repeating the
server default. Honors the spec's TIMESTAMP choice.
"""
from __future__ import annotations

from sqlalchemy import TIMESTAMP, func
from sqlalchemy.orm import mapped_column


def insert_timestamp(**kwargs):
    """Non-null TIMESTAMP defaulting to the DB clock at insert (started_at, recorded_at)."""
    return mapped_column(TIMESTAMP, server_default=func.now(), nullable=False, **kwargs)


def nullable_timestamp(**kwargs):
    """Nullable TIMESTAMP set explicitly by the application (ended_at)."""
    return mapped_column(TIMESTAMP, nullable=True, **kwargs)
