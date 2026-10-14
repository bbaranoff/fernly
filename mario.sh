#!/bin/bash
# Build, load and play the Mario port on the Fernvale from this terminal.
# Usage: ./mario.sh            (loads mario/mario.bin through fernly, then plays)
#        ./mario.sh --play     (skip loading, the game is already running)
set -e
cd "$(dirname "$0")"
PORT=${PORT:-/dev/ttyUSB0}
if [ "$1" != "--play" ]; then
    make -C mario >/dev/null
    python3 run_payload.py mario/mario.bin 0 "$PORT"
fi
exec python3 mario_play.py "$PORT"
