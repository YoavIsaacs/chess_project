"""Think-time arithmetic for the `time_taken_ms` column. Pure; no I/O.

The clock value in a MOVE packet is the player's remaining time *after* their
Fischer increment was applied on the press (firmware adds the increment on every
press, including the first). So the time actually consumed on the turn is

    prev_remaining - (curr_remaining - increment)  ==  prev - curr + increment

where prev_remaining is that player's clock at their previous move, or the
game's starting time for their first move (the caller supplies whichever).
"""
from __future__ import annotations


def think_time_ms(prev_remaining_ms: int, curr_remaining_ms: int, increment_ms: int) -> int:
    """Time consumed during the turn that produced `curr_remaining_ms`."""
    return prev_remaining_ms - curr_remaining_ms + increment_ms
