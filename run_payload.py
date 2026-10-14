#!/usr/bin/env python3
"""Reboot whatever runs on the Fernvale, load a payload through fernly and
print the payload's serial output for a few seconds.

The currently running firmware is kicked back to the boot ROM by sending one
byte (the bring-up reboots on any byte; fernly's shell gets "reboot").

Usage: run_payload.py PAYLOAD.bin [SECONDS] [PORT]
"""

import os
import select
import subprocess
import sys
import termios
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def open_port(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0
    a[1] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


def enumerations():
    return subprocess.run(["dmesg"], capture_output=True, text=True).stdout.count("usb 1-13: new full-speed")


def main():
    payload = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10
    port = sys.argv[3] if len(sys.argv) > 3 else "/dev/ttyUSB0"

    n0 = enumerations()
    if os.path.exists(port):
        try:
            fd = open_port(port)
            os.write(fd, b"reboot\nq")     # fernly shell: reboot; bring-up: any byte; mario: q
            os.close(fd)
            print("reboot request sent")
        except OSError as e:
            print("could not talk to the running firmware:", e)
    for _ in range(40):
        if enumerations() > n0:
            break
        time.sleep(0.5)
    else:
        print("board did not re-enumerate; replug it")
    for _ in range(30):
        if os.path.exists(port):
            break
        time.sleep(0.5)

    r = subprocess.run(["timeout", "90", os.path.join(HERE, "build/fernly-usb-loader"), "-w", port,
                        os.path.join(HERE, "build/usb-loader.bin"), os.path.join(HERE, "build/firmware.bin"), payload],
                       capture_output=True, text=True)
    tail = [l for l in r.stdout.replace("\r", "\n").splitlines() if l.strip()][-2:]
    print("loader rc=%d: %s" % (r.returncode, " | ".join(tail)))
    if r.returncode != 0:
        return 1

    # the payload re-initialises the USB device, which re-enumerates it:
    # wait for that before opening the (new) port node
    n1 = enumerations()
    for _ in range(30):
        if enumerations() > n1:
            break
        time.sleep(0.3)
    for _ in range(20):
        if os.path.exists(port):
            break
        time.sleep(0.5)
    time.sleep(0.5)
    if seconds <= 0:
        return 0
    fd = open_port(port)
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        rd, _, _ = select.select([fd], [], [], 0.2)
        if rd:
            try:
                buf += os.read(fd, 4096)
            except BlockingIOError:
                pass
            except OSError as e:
                print("read error:", e)
                break
    sys.stdout.write(buf.decode(errors="replace"))
    print("\n[%d bytes in %.0f s]" % (len(buf), seconds))
    return 0


if __name__ == "__main__":
    sys.exit(main())
