import pytest

from chess_db.enums import GameResult
from chess_db.mapping.result import result_from_wire


@pytest.mark.parametrize(
    "token, expected",
    [
        ("1-0", GameResult.white_wins),
        ("0-1", GameResult.black_wins),
        ("1/2", GameResult.draw),
        ("1-0T", GameResult.timeout_white),
        ("0-1T", GameResult.timeout_black),
    ],
)
def test_known_tokens_map(token, expected):
    assert result_from_wire(token) is expected


@pytest.mark.parametrize(
    "token",
    ["", "1-0t", "0-1t", "draw", "1-1", " 1-0", "1-0 ", "0-1 T", "1-0X"],
)
def test_unknown_tokens_raise(token):
    with pytest.raises(ValueError):
        result_from_wire(token)


def test_incomplete_is_not_wire_reachable():
    # `incomplete` is the creation-time default, never produced from a wire token.
    with pytest.raises(ValueError):
        result_from_wire("incomplete")
