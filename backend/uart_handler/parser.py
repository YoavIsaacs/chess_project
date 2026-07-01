# parser.py — Pure parse functions. Zero I/O.
# Input: raw line string from serial (newline already stripped).
# Output: typed dict with a "type" key, or raises ValueError on malformed input.


def parse_line(line: str) -> dict:
    """
    Dispatch a raw serial line to the appropriate parser.
    Returns a typed dict or raises ValueError.
    """
    line = line.strip()
    if not line:
        raise ValueError("Empty line")

    if line.startswith("GAME_START,"):
        return _parse_game_start(line)
    elif line.startswith("MOVE,"):
        return _parse_move(line)
    elif line.startswith("GAME_END,"):
        return _parse_game_end(line)
    else:
        raise ValueError(f"Unknown packet type: {line!r}")


def _parse_game_start(line: str) -> dict:
    # GAME_START,<white_first>,<white_last>,<black_first>,<black_last>,<time_ms>,<inc_ms>,<eval_visible>
    parts = line.split(",")
    if len(parts) != 8:
        raise ValueError(f"GAME_START expected 8 fields, got {len(parts)}: {line!r}")
    return {
        "type": "GAME_START",
        "white_first": parts[1],
        "white_last": parts[2],
        "black_first": parts[3],
        "black_last": parts[4],
        "time_ms": int(parts[5]),
        "inc_ms": int(parts[6]),
        "eval_visible": int(parts[7]),
    }


def _parse_move(line: str) -> dict:
    # MOVE,<move_num>,<W|B>,<white_ms>,<black_ms>,<mic_bar>,<temp_c>,<hum_pct>
    parts = line.split(",")
    if len(parts) != 8:
        raise ValueError(f"MOVE expected 8 fields, got {len(parts)}: {line!r}")
    player = parts[2]
    if player not in ("W", "B"):
        raise ValueError(f"MOVE player must be W or B, got {player!r}")
    return {
        "type": "MOVE",
        "move_num": int(parts[1]),
        "player": player,
        "white_ms": int(parts[3]),
        "black_ms": int(parts[4]),
        "mic_bar": int(parts[5]),
        "temp_c": int(parts[6]),
        "hum_pct": int(parts[7]),
    }


def _parse_game_end(line: str) -> dict:
    # GAME_END,<result>
    parts = line.split(",")
    if len(parts) != 2:
        raise ValueError(f"GAME_END expected 2 fields, got {len(parts)}: {line!r}")
    result = parts[1]
    valid_results = {"1-0", "0-1", "1/2", "1-0T", "0-1T"}
    if result not in valid_results:
        raise ValueError(f"GAME_END unknown result token {result!r}")
    return {
        "type": "GAME_END",
        "result": result,
    }
