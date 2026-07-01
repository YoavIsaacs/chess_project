import pytest
from fastapi import HTTPException

from api import dependencies


@pytest.fixture(autouse=True)
def _set_admin_token(monkeypatch):
    monkeypatch.setattr(dependencies, "ADMIN_TOKEN", "secret-token")


async def test_require_admin_accepts_correct_token():
    await dependencies.require_admin(authorization="Bearer secret-token")


async def test_require_admin_rejects_missing_header():
    with pytest.raises(HTTPException) as exc_info:
        await dependencies.require_admin(authorization=None)
    assert exc_info.value.status_code == 401


async def test_require_admin_rejects_malformed_header():
    with pytest.raises(HTTPException) as exc_info:
        await dependencies.require_admin(authorization="secret-token")
    assert exc_info.value.status_code == 401


async def test_require_admin_rejects_wrong_token():
    with pytest.raises(HTTPException) as exc_info:
        await dependencies.require_admin(authorization="Bearer wrong-token")
    assert exc_info.value.status_code == 403


async def test_require_admin_fails_closed_when_token_unset(monkeypatch):
    monkeypatch.setattr(dependencies, "ADMIN_TOKEN", "")
    with pytest.raises(HTTPException) as exc_info:
        await dependencies.require_admin(authorization="Bearer anything")
    assert exc_info.value.status_code == 403
