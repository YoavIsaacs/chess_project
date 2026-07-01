import pytest

from chess_db.enums import Player
from chess_db.mapping.player import player_from_wire


def test_white():
    assert player_from_wire("W") is Player.white


def test_black():
    assert player_from_wire("B") is Player.black


@pytest.mark.parametrize("token", ["w", "b", "", "White", "WB", " W", "1"])
def test_unknown_raises(token):
    with pytest.raises(ValueError):
        player_from_wire(token)
