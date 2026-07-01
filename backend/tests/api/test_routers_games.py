"""Public games router tests. Repository calls are monkeypatched; no DB needed."""
import uuid
from datetime import datetime, timezone

from api.routers import games as games_router_module
from chess_db.enums import GameResult, Player


def _make_game(game_id):
    class FakeGame:
        pass

    g = FakeGame()
    g.game_id = game_id
    g.white_first_name = "Magnus"
    g.white_last_name = "Carlsen"
    g.black_first_name = "Hikaru"
    g.black_last_name = "Nakamura"
    g.time_control = "3+2"
    g.eval_visible = True
    g.is_live = False
    g.result = GameResult.white_wins
    g.started_at = datetime(2026, 6, 1, tzinfo=timezone.utc)
    g.ended_at = datetime(2026, 6, 1, 0, 30, tzinfo=timezone.utc)
    return g


def _make_move(game_id, move_id):
    class FakeMove:
        pass

    m = FakeMove()
    m.move_id = move_id
    m.game_id = game_id
    m.move_number = 1
    m.move_notation = "e4"
    m.player = Player.white
    m.time_taken_ms = 1200
    m.clock_white_ms = 178800
    m.clock_black_ms = 180000
    m.eval_score = 0.3
    m.move_quality = None
    m.is_blunder = False
    m.noise_level = 12
    m.temperature_c = 22.5
    m.humidity_pct = 41.0
    m.recorded_at = datetime(2026, 6, 1, 0, 1, tzinfo=timezone.utc)
    return m


def test_list_games(client, monkeypatch):
    game = _make_game(uuid.uuid4())

    async def fake_list_games(session, limit=50, offset=0):
        return [game]

    monkeypatch.setattr(games_router_module.games_repo, "list_games", fake_list_games)

    resp = client.get("/games")

    assert resp.status_code == 200
    body = resp.json()
    assert len(body) == 1
    assert body[0]["white_first_name"] == "Magnus"
    assert body[0]["result"] == "white_wins"


def test_get_game_found(client, monkeypatch):
    game_id = uuid.uuid4()
    game = _make_game(game_id)

    async def fake_get_game(session, gid):
        return game

    monkeypatch.setattr(games_router_module.games_repo, "get_game", fake_get_game)

    resp = client.get(f"/games/{game_id}")

    assert resp.status_code == 200
    assert resp.json()["game_id"] == str(game_id)


def test_get_game_not_found(client, monkeypatch):
    async def fake_get_game(session, gid):
        return None

    monkeypatch.setattr(games_router_module.games_repo, "get_game", fake_get_game)

    resp = client.get(f"/games/{uuid.uuid4()}")

    assert resp.status_code == 404


def test_get_moves_found(client, monkeypatch):
    game_id = uuid.uuid4()
    game = _make_game(game_id)
    move = _make_move(game_id, uuid.uuid4())

    async def fake_get_game(session, gid):
        return game

    async def fake_list_moves(session, gid):
        return [move]

    monkeypatch.setattr(games_router_module.games_repo, "get_game", fake_get_game)
    monkeypatch.setattr(
        games_router_module.moves_repo, "list_moves_for_game", fake_list_moves
    )

    resp = client.get(f"/games/{game_id}/moves")

    assert resp.status_code == 200
    body = resp.json()
    assert len(body) == 1
    assert body[0]["move_notation"] == "e4"
    assert body[0]["player"] == "white"


def test_get_moves_game_not_found(client, monkeypatch):
    async def fake_get_game(session, gid):
        return None

    monkeypatch.setattr(games_router_module.games_repo, "get_game", fake_get_game)

    resp = client.get(f"/games/{uuid.uuid4()}/moves")

    assert resp.status_code == 404
