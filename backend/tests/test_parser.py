# test_parser.py — Unit tests for uart_handler/parser.py.
# parser.py is pure (zero I/O), so no mocking is needed.

import pytest
from uart_handler.parser import parse_line


# ── GAME_START ────────────────────────────────────────────────────────────────

class TestParseGameStart:

    def test_valid_returns_correct_type(self):
        result = parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000,1")
        assert result["type"] == "GAME_START"

    def test_valid_white_fields(self):
        result = parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000,1")
        assert result["white_first"] == "Alice"
        assert result["white_last"] == "Smith"

    def test_valid_black_fields(self):
        result = parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000,1")
        assert result["black_first"] == "Bob"
        assert result["black_last"] == "Jones"

    def test_valid_time_fields_are_int(self):
        result = parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000,1")
        assert result["time_ms"] == 600000
        assert result["inc_ms"] == 5000

    def test_valid_eval_visible_one(self):
        result = parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000,1")
        assert result["eval_visible"] == 1

    def test_valid_eval_visible_zero(self):
        result = parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000,0")
        assert result["eval_visible"] == 0

    def test_zero_increment(self):
        result = parse_line("GAME_START,A,B,C,D,300000,0,0")
        assert result["inc_ms"] == 0

    def test_too_few_fields_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000")

    def test_too_many_fields_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000,1,extra")

    def test_non_integer_time_ms_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_START,Alice,Smith,Bob,Jones,notanint,5000,1")

    def test_non_integer_inc_ms_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,notanint,1")

    def test_non_integer_eval_visible_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000,x")

    def test_leading_whitespace_stripped(self):
        result = parse_line("  GAME_START,Alice,Smith,Bob,Jones,600000,5000,1")
        assert result["type"] == "GAME_START"

    def test_trailing_whitespace_stripped(self):
        result = parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000,1  ")
        assert result["type"] == "GAME_START"

    def test_newline_stripped(self):
        result = parse_line("GAME_START,Alice,Smith,Bob,Jones,600000,5000,1\n")
        assert result["type"] == "GAME_START"


# ── MOVE ──────────────────────────────────────────────────────────────────────

class TestParseMove:

    def test_valid_white_returns_correct_type(self):
        result = parse_line("MOVE,1,W,295000,300000,3,22,55")
        assert result["type"] == "MOVE"

    def test_valid_white_player(self):
        result = parse_line("MOVE,1,W,295000,300000,3,22,55")
        assert result["player"] == "W"

    def test_valid_black_player(self):
        result = parse_line("MOVE,2,B,295000,298000,5,22,55")
        assert result["player"] == "B"

    def test_move_num_is_int(self):
        result = parse_line("MOVE,7,W,295000,300000,3,22,55")
        assert result["move_num"] == 7

    def test_times_are_int(self):
        result = parse_line("MOVE,1,W,295000,300000,3,22,55")
        assert result["white_ms"] == 295000
        assert result["black_ms"] == 300000

    def test_mic_bar_is_int(self):
        result = parse_line("MOVE,1,W,295000,300000,9,22,55")
        assert result["mic_bar"] == 9

    def test_temp_hum_are_int(self):
        result = parse_line("MOVE,1,W,295000,300000,3,22,55")
        assert result["temp_c"] == 22
        assert result["hum_pct"] == 55

    def test_mic_bar_zero(self):
        result = parse_line("MOVE,1,W,295000,300000,0,22,55")
        assert result["mic_bar"] == 0

    def test_invalid_player_x_raises(self):
        with pytest.raises(ValueError):
            parse_line("MOVE,1,X,295000,300000,3,22,55")

    def test_invalid_player_empty_raises(self):
        with pytest.raises(ValueError):
            parse_line("MOVE,1,,295000,300000,3,22,55")

    def test_invalid_player_lowercase_raises(self):
        with pytest.raises(ValueError):
            parse_line("MOVE,1,w,295000,300000,3,22,55")

    def test_too_few_fields_raises(self):
        with pytest.raises(ValueError):
            parse_line("MOVE,1,W,295000,300000,3,22")

    def test_too_many_fields_raises(self):
        with pytest.raises(ValueError):
            parse_line("MOVE,1,W,295000,300000,3,22,55,extra")

    def test_non_integer_move_num_raises(self):
        with pytest.raises(ValueError):
            parse_line("MOVE,one,W,295000,300000,3,22,55")

    def test_non_integer_white_ms_raises(self):
        with pytest.raises(ValueError):
            parse_line("MOVE,1,W,abc,300000,3,22,55")

    def test_non_integer_mic_bar_raises(self):
        with pytest.raises(ValueError):
            parse_line("MOVE,1,W,295000,300000,X,22,55")


# ── GAME_END ──────────────────────────────────────────────────────────────────

class TestParseGameEnd:

    @pytest.mark.parametrize("result_token", ["1-0", "0-1", "1/2", "1-0T", "0-1T"])
    def test_all_valid_result_tokens(self, result_token):
        result = parse_line(f"GAME_END,{result_token}")
        assert result["type"] == "GAME_END"
        assert result["result"] == result_token

    def test_too_few_fields_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_END")

    def test_too_many_fields_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_END,1-0,extra")

    def test_unknown_result_draw_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_END,draw")

    def test_unknown_result_asterisk_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_END,*")

    def test_unknown_result_empty_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_END,")

    def test_unknown_result_full_draw_notation_raises(self):
        with pytest.raises(ValueError):
            parse_line("GAME_END,1/2-1/2")


# ── Dispatch / top-level ───────────────────────────────────────────────────────

class TestParseLineDispatch:

    def test_empty_string_raises(self):
        with pytest.raises(ValueError):
            parse_line("")

    def test_whitespace_only_raises(self):
        with pytest.raises(ValueError):
            parse_line("   ")

    def test_unknown_prefix_raises(self):
        with pytest.raises(ValueError):
            parse_line("FOO,1,2,3")

    def test_eval_prefix_raises(self):
        # EVAL is Pi→STM32 only; STM32 never sends it
        with pytest.raises(ValueError):
            parse_line("EVAL,1,+35, !,0")

    def test_ack_prefix_raises(self):
        with pytest.raises(ValueError):
            parse_line("ACK,MOVE")
