#!/usr/bin/env python3
"""Try to start the MT6260 FD216 DSP from its mask ROM, the way the SIM800L
MAUI firmware does it (L1D_DSP_RestartDSP, L1D_HWCG_Init,
L1D_DSP_Dummy_WriteRead_DM, traditional_fd216_init, disassembled from
SIM800L16_PCB01_gprs_MT6260_S00.elf), without any DSP patch.

Sequence:
  1. HWCG init: hw 0x82c00058 &= ~0x20 ; w 0x83000400 |= 4 ; hw 0x82c00214 |= 0x200
  2. SHAREG2 0x83030000 = 6 then 7          (DSP reset release)
  3. dummy DM write/read at 0x82206000       (DM alive?)
  4. read 0x8200f000 ; hw 0x82ca0074 = 2
  5. DM 0x82207404 = 0 ; DM 0x82207406 = 0x7777   (handshake words)
  6. w 0x82000000 = 0                        (traditional_fd216_init)
  7. poll DM 0x82207404 == 0x8888 or DM 0x82207400 == 0x5555
  8. sample TDMA TQCNT

Needs the fernly build with `hexw` and `pokew`.  Usage: dsp_boot_probe.py PORT [OUT.md]
"""

import re
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0] or ".")
from regscan import Fernly, fmt  # noqa: E402


def main():
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    f = Fernly(port, timeout=3)
    if not f.sync():
        print("no prompt")
        return 1
    lines = ["\n## DSP boot probe (sequence from the SIM800L MT6260 firmware)\n"]

    def log(s=""):
        print(s, flush=True)
        lines.append(s)

    def hw(addr):
        out, ok = f.command("hexw 0x%08x 1" % addr)
        m = re.search(rb"[0-9a-f]{8}: ([0-9a-f]{4})", out)
        return int(m.group(1), 16) if m else None

    def pokew(addr, val):
        f.command("pokew 0x%08x 0x%x" % (addr, val & 0xffff))

    def dm_words(addr=0x82207400, n=8):
        out, ok = f.command("hexw 0x%08x %d" % (addr, n))
        m = re.search(rb"[0-9a-f]{8}:((?: [0-9a-f]{4})+)", out)
        return m.group(1).decode().strip() if m else "?"

    def tq(n=5):
        return " ".join(fmt(f.peek(0x83020000)) for _ in range(n))

    log("| step | observation |")
    log("|---|---|")
    log("| before | SHAREG2=%s DM@7400=`%s` TQCNT=%s |" % (fmt(hw(0x83030000)), dm_words(), tq(3)))

    v = hw(0x82c00058); pokew(0x82c00058, v & ~0x20)
    w = f.peek(0x83000400); f.command("poke 0x83000400 0x%x" % (w | 4))
    v2 = hw(0x82c00214); pokew(0x82c00214, v2 | 0x200)
    log("| 1 HWCG init | 0x82c00058 %s->%s, 0x83000400 %s->%s, 0x82c00214 %s->%s |" % (
        fmt(v), fmt(hw(0x82c00058)), fmt(w), fmt(f.peek(0x83000400)), fmt(v2), fmt(hw(0x82c00214))))

    pokew(0x83030000, 6); time.sleep(0.01); pokew(0x83030000, 7)
    log("| 2 SHAREG2 6 then 7 | readback %s, TQCNT=%s |" % (fmt(hw(0x83030000)), tq(3)))

    ok = False
    for _ in range(5):
        pokew(0x82206000, 0x5555); pokew(0x82206002, 0x6666)
        if hw(0x82206000) == 0x5555 and hw(0x82206002) == 0x6666:
            ok = True
            break
    pokew(0x82206000, 0xaaaa)
    log("| 3 dummy DM write/read @0x82206000 | %s (readback %s %s) |" % (
        "DM alive" if ok else "DM NOT writable", fmt(hw(0x82206000)), fmt(hw(0x82206002))))

    x = f.peek(0x8200f000); pokew(0x82ca0074, 2)
    log("| 4 read 0x8200f000, hw 0x82ca0074=2 | 0x8200f000=%s, 0x82ca0074 readback %s |" % (fmt(x), fmt(hw(0x82ca0074))))

    pokew(0x82207404, 0); pokew(0x82207406, 0x7777)
    log("| 5 handshake words | DM@7400=`%s` |" % dm_words())

    f.command("poke 0x82000000 0x0")
    log("| 6 traditional_fd216_init: w 0x82000000=0 | readback %s |" % fmt(f.peek(0x82000000)))

    seen = None
    t0 = time.time()
    while time.time() - t0 < 3.0:
        a, b = hw(0x82207404), hw(0x82207400)
        if a == 0x8888 or b == 0x5555:
            seen = "DSP answered: DM@7404=%s DM@7400=%s" % (fmt(a), fmt(b))
            break
    log("| 7 poll for 0x8888/0x5555 (3 s) | %s |" % (seen or ("no answer, DM@7400=`%s`" % dm_words())))
    log("| 8 TQCNT after | %s |" % tq(6))
    log("| SM window 0x82300000 head | `%s` |" % dm_words(0x82300000, 8))
    log()
    if out:
        with open(out, "a") as fh:
            fh.write("\n".join(lines) + "\n")
        print("appended to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
