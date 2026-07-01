import pytest

from api import game_registry


@pytest.fixture(autouse=True)
def _reset_registry():
    """Cross-test isolation for the module-level dict, mirroring the
    reset_game_state autouse fixture used for uart_handler's game_state.py."""
    game_registry._increments.clear()
    yield
    game_registry._increments.clear()


def test_set_and_get_increment():
    game_registry.set_increment("game-1", 2000)
    assert game_registry.get_increment("game-1") == 2000


def test_get_increment_defaults_to_zero_for_unknown_game():
    assert game_registry.get_increment("nonexistent") == 0


def test_clear_increment_removes_entry():
    game_registry.set_increment("game-2", 5000)
    game_registry.clear_increment("game-2")
    assert game_registry.get_increment("game-2") == 0


def test_clear_increment_is_safe_on_missing_key():
    game_registry.clear_increment("never-set")  # must not raise
