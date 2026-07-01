"""Custom column types."""
from __future__ import annotations

import uuid

from sqlalchemy.types import CHAR, TypeDecorator


class GUID(TypeDecorator):
    """UUID stored as a hyphenated 36-character string (CHAR(36)).

    Chosen over BINARY(16) for debuggability: a game_id is readable directly in
    query output and maps 1:1 to the UUID4 strings the UART Handler generates and
    to Redis channel names, with no encode/decode at any boundary.

    Accepts a `uuid.UUID` or any string parseable as one (validating and
    canonicalising it); always returns a `uuid.UUID`.
    """

    impl = CHAR(36)
    cache_ok = True

    def process_bind_param(self, value, dialect):
        if value is None:
            return None
        if isinstance(value, uuid.UUID):
            return str(value)
        return str(uuid.UUID(str(value)))

    def process_result_value(self, value, dialect):
        if value is None:
            return None
        return uuid.UUID(value)
