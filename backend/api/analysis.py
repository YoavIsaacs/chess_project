"""Stockfish integration stub — the real implementation lands in Step 16.

`request_eval` is called after every move is persisted so the wiring exists
end-to-end now; for the moment it only logs. When Step 16 lands, this will
run Stockfish on the resulting position, publish an EVAL payload to
`game:{game_id}:eval` for the UART Handler's redis_bridge to relay to the
STM32, and write eval_score/move_quality/is_blunder back onto the Move row
(needs a new `update_move` repository function — none exists yet, since
move rows are currently insert-only). eval_score conversion already has a
home: chess_db.mapping.eval_score.pawns_from_centipawns.
"""
import logging

log = logging.getLogger(__name__)


async def request_eval(game_id: str, move) -> None:
    log.debug(
        "request_eval stub called for game %s, move %s (Step 16 TODO)",
        game_id,
        move.move_id,
    )
