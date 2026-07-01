"""Game model -> `games` table (spec section 3.4, Game Record).

No `from __future__ import annotations` here: stringized annotations interact
awkwardly with SQLAlchemy relationship resolution, so model modules use real
annotations with quoted forward refs for relationship targets.
"""
import uuid
from datetime import datetime

import sqlalchemy as sa
from sqlalchemy import Enum, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from ..base import Base
from ..enums import GameResult
from ..mixins import insert_timestamp, nullable_timestamp
from ..types import GUID


class Game(Base):
    __tablename__ = "games"

    # Externally supplied: the UART Handler generates the UUID4 at GAME_START and
    # carries it on Redis. No default here, so a missing id surfaces as an error
    # rather than being silently invented.
    game_id: Mapped[uuid.UUID] = mapped_column(GUID, primary_key=True)

    white_first_name: Mapped[str] = mapped_column(String(12), nullable=False)
    white_last_name: Mapped[str] = mapped_column(String(12), nullable=False)
    black_first_name: Mapped[str] = mapped_column(String(12), nullable=False)
    black_last_name: Mapped[str] = mapped_column(String(12), nullable=False)

    time_control: Mapped[str] = mapped_column(String(20), nullable=False)
    eval_visible: Mapped[bool] = mapped_column(sa.Boolean, nullable=False)

    # A row is born live and incomplete; end_game flips both. Server defaults
    # encode that invariant even though create_game also sets them explicitly.
    is_live: Mapped[bool] = mapped_column(
        sa.Boolean, nullable=False, server_default=sa.true()
    )
    result: Mapped[GameResult] = mapped_column(
        Enum(
            GameResult,
            name="game_result",
            values_callable=lambda enum_cls: [m.value for m in enum_cls],
        ),
        nullable=False,
        server_default=sa.text("'incomplete'"),
    )

    started_at: Mapped[datetime] = insert_timestamp()
    ended_at: Mapped[datetime | None] = nullable_timestamp()

    moves: Mapped[list["Move"]] = relationship(
        back_populates="game",
        cascade="all, delete-orphan",
        passive_deletes=True,
        order_by="Move.move_number",
    )
