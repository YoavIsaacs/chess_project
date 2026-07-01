"""Wire-format formatting not covered by chess_db.mapping.

Player, result, and ply conversions moved to chess_db.mapping.{player,result,ply}
directly once those files were available -- this module now only holds
format_time_control, which isn't a mapping concern (nothing on the wire
carries a formatted time-control string; it's assembled here for display).
"""
from __future__ import annotations


def format_time_control(time_ms: int, inc_ms: int) -> str:
    """Format base+increment as 'M+S' (minutes base, seconds increment).

    The spec's example ('Blitz 3+2') includes a category label, but no
    time-control classification thresholds (what counts as Blitz vs Rapid vs
    Classical) are defined anywhere in the spec or codebase, so only the
    base+increment pair is rendered here. Comfortably fits VARCHAR(20); add a
    category prefix later if you want one.
    """
    minutes = time_ms // 60_000
    inc_seconds = inc_ms // 1_000
    return f"{minutes}+{inc_seconds}"
