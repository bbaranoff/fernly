#!/usr/bin/env python3
"""Play the Fernvale Mario port from this terminal: arrow keys move and jump,
space jumps, b runs, q quits (reboots the board into the boot ROM).
Game log lines are shown as they arrive.  Usage: mario_play.py [PORT]"""
import os, select, sys, termios, tty, time

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd); a[0] = 0; a[1] = 0; a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL; a[3] = 0
termios.tcsetattr(fd, termios.TCSANOW, a)
old = termios.tcgetattr(sys.stdin.fileno())
tty.setraw(sys.stdin.fileno())
keymap = {b"\x1b[D": b"a", b"\x1b[C": b"d", b"\x1b[A": b"w", b" ": b"w", b"b": b"b", b"q": b"q"}
try:
    sys.stdout.write("\r\n[fleches: bouger/sauter, espace: sauter, b: courir, q: quitter]\r\n")
    pending = b""
    while True:
        r, _, _ = select.select([sys.stdin, fd], [], [], 0.05)
        if sys.stdin in r:
            pending += os.read(sys.stdin.fileno(), 16)
            while pending:
                for seq, cmd in keymap.items():
                    if pending.startswith(seq):
                        try: os.write(fd, cmd)
                        except OSError: pass
                        pending = pending[len(seq):]
                        if cmd == b"q":
                            raise KeyboardInterrupt
                        break
                else:
                    if pending == b"\x1b" or pending.startswith(b"\x1b[") and len(pending) < 3:
                        break        # incomplete escape sequence, wait for more
                    pending = pending[1:]
        if fd in r:
            try:
                data = os.read(fd, 4096)
                sys.stdout.write(data.decode(errors="replace").replace("\n", "\r\n")); sys.stdout.flush()
            except (BlockingIOError, OSError):
                pass
except KeyboardInterrupt:
    pass
finally:
    termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, old)
    print("\nbye")
