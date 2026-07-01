"""Move model -> `moves` table (spec section 3.4, Move Record).

`move_number` stores a true monotonic ply (see mapping/ply.py): it is the column
the spec calls "Ply number". The composite (game_id, move_number) index serves
the dominant query — a game's moves in order — and its leftmost prefix covers
plain game_id lookups, so no separate single-column index is needed.
"""
import uuid
from datetime import datetime

import sqlalchemy as sa
from sqlalchemy import Enum, ForeignKey, Index, String
from sqlalchemy.orm import Mapped, mapped_column, relationship

from ..base import Base
from ..enums import Player
from ..mixins import insert_timestamp
from ..types import GUID


class Move(Base):
    __tablename__ = "moves"
    __table_args__ = (
        Index("ix_moves_game_id_move_number", "game_id", "move_number"),
    )

    # Locally generated: the wire carries no move id, so the DB owns it.
    move_id: Mapped[uuid.UUID] = mapped_column(
        GUID, primary_key=True, default=uuid.uuid4
    )
    game_id: Mapped[uuid.UUID] = mapped_column(
        GUID,
        ForeignKey("games.game_id", ondelete="CASCADE"),
        nullable=False,
    )

    move_number: Mapped[int] = mapped_column(sa.Integer, nullable=False)
    move_notation: Mapped[str] = mapped_column(String(10), nullable=False)
    player: Mapped[Player] = mapped_column(
        Enum(
            Player,
            name="player_color",
            values_callable=lambda enum_cls: [m.value for m in enum_cls],
        ),
        nullable=False,
    )

    time_taken_ms: Mapped[int] = mapped_column(sa.Integer, nullable=False)
    clock_white_ms: Mapped[int] = mapped_column(sa.Integer, nullable=False)
    clock_black_ms: Mapped[int] = mapped_column(sa.Integer, nullable=False)

    # Eval, quality and sensor readings arrive on separate channels (EVAL /
    # sensor_data) and may not be present when a move is first logged, so they
    # are nullable. is_blunder defaults false (a move is not a blunder until
    # flagged).
    eval_score: Mapped[float | None] = mapped_column(sa.Float, nullable=True)
    move_quality: Mapped[str | None] = mapped_column(String(2), nullable=True)
    is_blunder: Mapped[bool] = mapped_column(
        sa.Boolean, nullable=False, server_default=sa.false()
    )
    noise_level: Mapped[int | None] = mapped_column(sa.Integer, nullable=True)
    temperature_c: Mapped[float | None] = mapped_column(sa.Float, nullable=True)
    humidity_pct: Mapped[float | None] = mapped_column(sa.Float, nullable=True)

    recorded_at: Mapped[datetime] = insert_timestamp()

    game: Mapped["Game"] = relationship(back_populates="moves")
