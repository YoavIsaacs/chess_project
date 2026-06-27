from chess_db.mapping.timing import think_time_ms


def test_midgame_move():
    # 180s start, consumed 5s, +2s increment -> packet shows 177s; think = 5s.
    assert think_time_ms(180_000, 177_000, 2_000) == 5_000


def test_first_move_uses_starting_time_as_prev():
    # First White move: prev = starting time 180s; thought 3s; +2s -> packet 179s.
    assert think_time_ms(180_000, 179_000, 2_000) == 3_000


def test_instant_move_is_zero():
    # Pressed immediately: 0s consumed, only the increment moved the clock.
    assert think_time_ms(180_000, 182_000, 2_000) == 0


def test_zero_increment():
    assert think_time_ms(900_000, 890_000, 0) == 10_000
