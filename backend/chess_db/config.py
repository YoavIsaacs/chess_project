"""Database configuration constants.

All connection parameters are read from the environment so credentials never
live in source control. Defaults assume a local MySQL instance; test and
production values are supplied via env vars.
"""
from __future__ import annotations

import os

from sqlalchemy import URL

# The asyncmy trap (analogous to pyserial-not-serial): the dialect string must
# be exactly this, or SQLAlchemy silently selects the sync driver.
DB_DIALECT = "mysql+asyncmy"

DB_USER = os.environ.get("CHESS_DB_USER", "chess")
DB_PASSWORD = os.environ.get("CHESS_DB_PASSWORD", "")
DB_HOST = os.environ.get("CHESS_DB_HOST", "127.0.0.1")
DB_PORT = int(os.environ.get("CHESS_DB_PORT", "3306"))
DB_NAME = os.environ.get("CHESS_DB_NAME", "chess")

ECHO = os.environ.get("CHESS_DB_ECHO", "0") == "1"
POOL_SIZE = int(os.environ.get("CHESS_DB_POOL_SIZE", "5"))
POOL_MAX_OVERFLOW = int(os.environ.get("CHESS_DB_POOL_MAX_OVERFLOW", "10"))


def build_url() -> URL:
    """Construct the SQLAlchemy async URL from the configured parameters.

    Built via URL.create (not an f-string) so special characters in the password
    are escaped correctly.
    """
    return URL.create(
        drivername=DB_DIALECT,
        username=DB_USER,
        password=DB_PASSWORD or None,
        host=DB_HOST,
        port=DB_PORT,
        database=DB_NAME,
    )
