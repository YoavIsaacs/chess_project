import pytest

from chess_db.mapping.eval_score import MATE_SENTINEL_CP, pawns_from_centipawns


@pytest.mark.parametrize(
    "cp, expected",
    [
        (0, 0.0),
        (35, 0.35),
        (-180, -1.8),
        (125, 1.25),
        (-300, -3.0),
        (1, 0.01),
    ],
)
def test_conversion(cp, expected):
    assert pawns_from_centipawns(cp) == pytest.approx(expected)


def test_returns_float():
    assert isinstance(pawns_from_centipawns(35), float)


def test_mate_sentinel_current_behavior():
    # Documents the *deferred* behavior: the sentinel saturates to +/-327.67
    # rather than being represented as a mate distance. Revisit in Step 16.
    assert pawns_from_centipawns(MATE_SENTINEL_CP) == pytest.approx(327.67)
    assert pawns_from_centipawns(-MATE_SENTINEL_CP) == pytest.approx(-327.67)
