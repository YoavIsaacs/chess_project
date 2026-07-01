# conftest.py — Shared fixtures for uart_handler test suite.

import pytest
import uart_handler.game_state as game_state


@pytest.fixture(autouse=True)
def reset_game_state():
    """Reset shared mutable state before every test. Prevents cross-test pollution."""
    game_state.game_id = None
    game_state.active = False
    yield
    game_state.game_id = None
    game_state.active = False
