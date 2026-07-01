import sys
import pathlib

# Insert backend/ itself (this file's directory) onto sys.path, not
# uart_handler/. This is what makes `uart_handler.<module>`, `chess_db.<module>`,
# and `api.<module>` all importable by their full package path from anywhere
# under tests/. Pytest's own conftest-rootdir insertion already does this
# implicitly in most cases, but making it explicit here means the test suite
# doesn't depend on that implicit behavior, and avoids the previous version's
# bug: inserting uart_handler/ directly at sys.path[0] made bare imports like
# `import config` silently resolve to uart_handler/config.py ahead of
# anything else on the path, which is a landmine for api/ and chess_db/ test
# code (or anything else) that does a bare same-named import.
sys.path.insert(0, str(pathlib.Path(__file__).parent))
