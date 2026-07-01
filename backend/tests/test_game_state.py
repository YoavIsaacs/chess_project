# test_game_state.py — Unit tests for uart_handler/game_state.py.
# The autouse fixture in conftest.py resets both vars before every test.

import uart_handler.game_state as game_state


class TestGameStateDefaults:

    def test_game_id_is_none_by_default(self):
        assert game_state.game_id is None

    def test_active_is_false_by_default(self):
        assert game_state.active is False


class TestGameStateMutation:

    def test_game_id_can_be_set(self):
        game_state.game_id = "test-uuid"
        assert game_state.game_id == "test-uuid"

    def test_active_can_be_set_true(self):
        game_state.active = True
        assert game_state.active is True

    def test_active_can_be_cleared(self):
        game_state.active = True
        game_state.active = False
        assert game_state.active is False

    def test_game_id_and_active_are_independent(self):
        game_state.game_id = "abc-123"
        game_state.active = False
        assert game_state.game_id == "abc-123"
        assert game_state.active is False


class TestGameStateIsolation:
    """Two consecutive tests that would interfere if the conftest fixture were absent."""

    def test_isolation_first(self):
        game_state.game_id = "first-test-id"
        game_state.active = True

    def test_isolation_second(self):
        assert game_state.game_id is None
        assert game_state.active is False
