"""Pydantic response models for the public and admin REST API.

`result` and `player` are plain `enum.Enum` (not `str, enum.Enum`) on the ORM
side, so a `from_attributes` build would hand pydantic an enum member for a
`str` field — the `mode="before"` validators unwrap `.value` so serialization
just works instead of raising.
"""
from __future__ import annotations

import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict, field_validator

from chess_db.enums import GameResult, Player


class GameOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    game_id: uuid.UUID
    white_first_name: str
    white_last_name: str
    black_first_name: str
    black_last_name: str
    time_control: str
    eval_visible: bool
    is_live: bool
    result: str
    started_at: datetime
    ended_at: datetime | None

    @field_validator("result", mode="before")
    @classmethod
    def _result_to_str(cls, v):
        return v.value if isinstance(v, GameResult) else v


class MoveOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    move_id: uuid.UUID
    game_id: uuid.UUID
    move_number: int
    move_notation: str
    player: str
    time_taken_ms: int
    clock_white_ms: int
    clock_black_ms: int
    eval_score: float | None
    move_quality: str | None
    is_blunder: bool
    noise_level: int | None
    temperature_c: float | None
    humidity_pct: float | None
    recorded_at: datetime

    @field_validator("player", mode="before")
    @classmethod
    def _player_to_str(cls, v):
        return v.value if isinstance(v, Player) else v
