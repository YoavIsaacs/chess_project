"""Shared fixtures for the API test suite.

These tests exercise routers and helpers in isolation — no real MySQL or
Redis connection is ever opened. Repository calls are monkeypatched per
test; get_db_session is overridden with a dummy session since the mocked
repo functions never actually touch it.
"""
from __future__ import annotations

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from api.dependencies import get_db_session
from api.routers.admin import router as admin_router
from api.routers.games import router as games_router


async def _fake_session():
    yield object()


@pytest.fixture
def test_app() -> FastAPI:
    app = FastAPI()
    app.include_router(games_router)
    app.include_router(admin_router)
    app.dependency_overrides[get_db_session] = _fake_session
    return app


@pytest.fixture
def client(test_app: FastAPI) -> TestClient:
    return TestClient(test_app)
