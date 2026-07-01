"""ORM models (one per table). Importing this registers both mappers and
populates Base.metadata, which is what create_all and Alembic autogenerate read.
"""
from .game import Game
from .move import Move

__all__ = ["Game", "Move"]
