import pytest

from chess_db.enums import Player
from chess_db.mapping.ply import ply_from_pair


@pytest.mark.parametrize(
    "move_number, player, expected",
    [
        (1, Player.white, 1),
        (1, Player.black, 2),
        (2, Player.white, 3),
        (2, Player.black, 4),
        (10, Player.white, 19),
        (10, Player.black, 20),
    ],
)
def test_ply(move_number, player, expected):
    assert ply_from_pair(move_number, player) == expected


def test_ply_is_strictly_increasing_over_a_game():
    seq = []
    for n in range(1, 6):
        seq.append(ply_from_pair(n, Player.white))
        seq.append(ply_from_pair(n, Player.black))
    assert seq == list(range(1, 11))


def test_pair_number_recoverable_from_ply():
    for n in range(1, 6):
        for player in (Player.white, Player.black):
            ply = ply_from_pair(n, player)
            assert (ply + 1) // 2 == n


@pytest.mark.parametrize("bad", [0, -1, -10])
def test_invalid_move_number_raises(bad):
    with pytest.raises(ValueError):
        ply_from_pair(bad, Player.white)
