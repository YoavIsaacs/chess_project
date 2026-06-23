"""
Step 10a - Minimal Pi-side UART test script.

Purpose: validate the STM32 transmit side once the replacement board
arrives. Listens on the Pi's primary UART, ACKs every packet it
receives, and replies with a synthetic EVAL line after every MOVE
packet, cycling through a fixed 5-entry mock list.

No Redis, no Stockfish, no FastAPI, no MySQL - this is a standalone
test endpoint only. The production UART Handler process is Step 12.
"""

import serial

SERIAL_PORT = "/dev/serial0"
BAUD_RATE = 115200

# Mock EVAL replies, cycled in order on every MOVE packet received.
# Mirrors the embedded mock state coverage from Step 8 (k_mock_eval_cp[]):
# zero, small positive, negative + blunder, near-saturating, forced mate.
MOCK_EVALS = [
    {"move": "e4",    "eval_cp": 0,     "quality": "  ", "is_blunder": 0},
    {"move": "Nf3",   "eval_cp": 35,    "quality": " !", "is_blunder": 0},
    {"move": "Qxd5",  "eval_cp": -180,  "quality": "??", "is_blunder": 1},
    {"move": "O-O-O", "eval_cp": 480,   "quality": "!!", "is_blunder": 0},
    {"move": "Rxf7#", "eval_cp": 32767, "quality": " !", "is_blunder": 0},
]

VALID_PACKET_TYPES = {"GAME_START", "MOVE", "GAME_END"}


def build_eval_line(entry):
    """Format one MOCK_EVALS entry as a wire-format EVAL line."""
    eval_cp_str = f"{entry['eval_cp']:+d}"
    return f"EVAL,{entry['move']},{eval_cp_str},{entry['quality']},{entry['is_blunder']}\n"


def main():
    mock_index = 0

    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=None)
    print(f"[UART_TEST] Listening on {SERIAL_PORT} at {BAUD_RATE} baud")

    try:
        while True:
            raw_line = ser.readline()
            if not raw_line:
                continue

            line = raw_line.decode("ascii", errors="replace").strip()
            if not line:
                continue

            parts = line.split(",")
            packet_type = parts[0]

            if packet_type not in VALID_PACKET_TYPES:
                print(f"[UART_TEST] Ignoring unrecognised packet: {line!r}")
                continue

            print(f"[UART_TEST] RX {packet_type}: {line}")

            ack_line = f"ACK,{packet_type}\n"
            ser.write(ack_line.encode("ascii"))
            print(f"[UART_TEST] TX {ack_line.strip()}")

            if packet_type == "MOVE":
                entry = MOCK_EVALS[mock_index]
                eval_line = build_eval_line(entry)
                ser.write(eval_line.encode("ascii"))
                print(f"[UART_TEST] TX {eval_line.strip()}")
                mock_index = (mock_index + 1) % len(MOCK_EVALS)

    except KeyboardInterrupt:
        print("\n[UART_TEST] Stopped.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
