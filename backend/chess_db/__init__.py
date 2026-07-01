"""chess_db — persistence layer for the chess analysis system.

Kept import-light on purpose: importing this package must not construct the
engine (which imports the asyncmy DBAPI). Import the specific submodule you
need — e.g. `from chess_db.engine import SessionMaker`.
"""
