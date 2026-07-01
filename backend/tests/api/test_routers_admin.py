"""Admin router tests — auth enforcement + mutating endpoints."""
import uuid
from datetime import datetime, timezone

import pytest

import api.dependencies as dependencies_module
from api.routers import admin as admin_router_module
from chess_db.enums import GameResult

ADMIN_HEADER = {"Authorization": "Bearer test-admin-token"}


@pytest.fixture(autouse=True)
def _set_admin_token(monkeypatch):
    monkeypatch.setattr(dependencies_module, "ADMIN_TOKEN", "test-admin-token")


def test_delete_game_requires_auth(client):
    resp = client.delete(f"/admin/games/{uuid.uuid4()}")
    assert resp.status_code == 401


def test_delete_game_rejects_wrong_token(client):
    resp = client.delete(
        f"/admin/games/{uuid.uuid4()}",
        headers={"Authorization": "Bearer wrong"},
    )
    assert resp.status_code == 403


def test_delete_game_success(client, monkeypatch):
    async def fake_delete_game(session, gid):
        return True

    monkeypatch.setattr(admin_router_module.games_repo, "delete_game", fake_delete_game)

    resp = client.delete(f"/admin/games/{uuid.uuid4()}", headers=ADMIN_HEADER)

    assert resp.status_code == 204


def test_delete_game_not_found(client, monkeypatch):
    async def fake_delete_game(session, gid):
        return False

    monkeypatch.setattr(admin_router_module.games_repo, "delete_game", fake_delete_game)

    resp = client.delete(f"/admin/games/{uuid.uuid4()}", headers=ADMIN_HEADER)

    assert resp.status_code == 404


def test_force_end_game_success(client, monkeypatch):
    game_id = uuid.uuid4()

    class FakeGame:
        pass

    g = FakeGame()
    g.game_id = game_id
    g.white_first_name = "A"
    g.white_last_name = "B"
    g.black_first_name = "C"
    g.black_last_name = "D"
    g.time_control = "3+2"
    g.eval_visible = True
    g.is_live = False
    g.result = GameResult.timeout_white
    g.started_at = datetime(2026, 6, 1, tzinfo=timezone.utc)
    g.ended_at = datetime(2026, 6, 1, 0, 5, tzinfo=timezone.utc)

    async def fake_end_game(session, gid, result, ended_at):
        return g

    monkeypatch.setattr(admin_router_module.games_repo, "end_game", fake_end_game)

    resp = client.post(
        f"/admin/games/{game_id}/force-end",
        headers=ADMIN_HEADER,
        json={"result": "timeout_white"},
    )

    assert resp.status_code == 200
    assert resp.json()["result"] == "timeout_white"


def test_force_end_game_not_found(client, monkeypatch):
    async def fake_end_game(session, gid, result, ended_at):
        raise ValueError("no game")

    monkeypatch.setattr(admin_router_module.games_repo, "end_game", fake_end_game)

    resp = client.post(
        f"/admin/games/{uuid.uuid4()}/force-end",
        headers=ADMIN_HEADER,
        json={"result": "draw"},
    )

    assert resp.status_code == 404
