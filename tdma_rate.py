#!/usr/bin/env python3
"""Measure the TDMA quarter-bit rate through fernly, using the TOPSM
microsecond counter as time base, then try a clock tweak and remeasure.

The phase advance of TQCNT over one second is 3333 (mod 5000) at the nominal
1083333 qbit/s and 1667 at half rate, so one second of waiting is enough to
tell the two apart.  Usage: tdma_rate.py PORT [OUT.md]"""
import sys, time
sys.path.insert(0, __file__.rsplit("/", 1)[0] or ".")
from regscan import Fernly, fmt

T, MD, USC = 0x83020000, 0x83000000, 0xa0180088

def main():
    f = Fernly(sys.argv[1], timeout=3)
    assert f.sync()
    out = sys.argv[2] if len(sys.argv) > 2 else None
    lines = ["\n## TDMA rate vs PLL_CON4 bit 2 (fernly, USC time base)\n"]

    def log(s=""):
        print(s, flush=True)
        lines.append(s)

    def measure():
        us0, q0 = f.peek(USC), f.peek(T)
        time.sleep(1.0)
        us1, q1 = f.peek(USC), f.peek(T)
        dus = us1 - us0
        dq = (q1 - q0) % 5000
        nom = (1083333 * dus / 1e6) % 5000
        half = (541667 * dus / 1e6) % 5000
        err_n = min(abs(dq - nom), 5000 - abs(dq - nom))
        err_h = min(abs(dq - half), 5000 - abs(dq - half))
        verdict = "NOMINAL" if err_n < err_h else "HALF"
        return "dus=%d dq=%d (nominal would be %d, half %d) -> %s" % (dus, dq, nom, half, verdict)

    f.command("poke 0xa0180080 0x11530001")          # USC start
    # same sequence as md2g_power_on() in the bring-up firmware
    import re as _re
    def hw(a):
        o, ok = f.command("hexw 0x%08x 1" % a); m = _re.search(rb"[0-9a-f]{8}: ([0-9a-f]{4})", o); return int(m.group(1), 16)
    f.command("pokew 0x82c00058 0x%x" % (hw(0x82c00058) & ~0x20))
    f.command("poke 0x83000400 0x%x" % ((f.peek(0x83000400) & 0xffff) | 4))
    f.command("pokew 0x82c00214 0x%x" % (hw(0x82c00214) | 0x200))
    f.command("poke 0xa0010320 0x100")                 # PD_MD2G_26M off
    f.command("poke 0x83000028 0x0"); f.command("poke 0x83000018 0x0")
    f.command("pokew 0x82c00010 0x2"); f.command("pokew 0x82c00018 0x0")
    f.command("pokew 0x82c00020 0x0"); f.command("pokew 0x82c00028 0x220")
    time.sleep(0.1)
    running = f.peek(T) != f.peek(T) or f.peek(T) != f.peek(T)
    log("Counter running after the md2g_power_on sequence: %s" % running)
    if not running:
        log("counter not running, aborting"); open(out, "a").write("\n".join(lines) + "\n") if out else None; return 1
    log("| step | PLL_CON4 | measurement |")
    log("|---|---|---|")
    log("| modem powered, counter running? | %s | %s |" % (fmt(f.peek(0xa0170050)), measure()))
    v = f.peek(0xa0170050)
    f.command("pokew 0xa0170050 0x%x" % (v & ~4))
    time.sleep(0.1)
    log("| PLL_CON4 bit 2 cleared | %s | %s |" % (fmt(f.peek(0xa0170050)), measure()))
    log("| again 1 s later | %s | %s |" % (fmt(f.peek(0xa0170050)), measure()))
    f.command("pokew 0xa0170050 0x%x" % v)
    time.sleep(0.1)
    log("| PLL_CON4 restored | %s | %s |" % (fmt(f.peek(0xa0170050)), measure()))
    log()
    if out:
        open(out, "a").write("\n".join(lines) + "\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
