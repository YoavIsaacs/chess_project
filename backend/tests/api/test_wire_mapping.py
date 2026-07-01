"""wire_mapping now only formats time control -- player/result conversion
tests moved with the logic to chess_db.mapping (tested there already)."""
import pytest

from api import wire_mapping


@pytest.mark.parametrize(
    "time_ms,inc_ms,expected",
    [
        (180000, 2000, "3+2"),
        (600000, 0, "10+0"),
        (60000, 1000, "1+1"),
    ],
)
def test_format_time_control(time_ms, inc_ms, expected):
    assert wire_mapping.format_time_control(time_ms, inc_ms) == expected
