#!/usr/bin/env python3
"""Try to wake the MT6260 modem blocks and watch the TDMA quarter-bit counter.

Hypothesis (from osmocom-bb include/mtk/system.h, written for the MT6235):
the CONFG block holds PDN_CON0..3 at +0x300..0x30c with SET/CLR mirrors at
+0x310/+0x320, and PDN_CON2 bit0 = TDMA, bit1 = RTC, bit2 = BSI, bit3 = BPI,
bit4 = AFC, bit5 = APC.  On the MT6260 the CONFG block sits at 0xa0010000
(fernly README) and the TDMA unit at 0x83020000, with TQCNT at +0.

Usage: tdma_probe.py PORT [OUT.md]
"""

import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0] or ".")
from regscan import Fernly, fmt  # noqa: E402

CONFG = 0xa0010000
TDMA = 0x83020000
BLOCKS = {"TDMA": 0x83020000, "FCS": 0x83050000, "GPRS cipher": 0x83060000,
          "BSI": 0x83070000, "BPI": 0x83080000}


def main():
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    f = Fernly(port, timeout=3)
    if not f.sync():
        print("no prompt")
        return 1
    lines = ["## TDMA wake-up probe", ""]

    def log(s):
        print(s, flush=True)
        lines.append(s)

    log("### CONFG block 0xa0010000 before")
    log("")
    log("| offset | value | MT6235 name |")
    log("|---|---|---|")
    names = {0x000: "HW_VERSION", 0x004: "FW_VERSION", 0x008: "HW_CODE",
             0x114: "SLEEP_CON", 0x118: "MCUCLK_CON", 0x11c: "DSPCLK_CON",
             0x200: "IDN_SEL", 0x300: "PDN_CON0", 0x304: "PDN_CON1",
             0x308: "PDN_CON2", 0x30c: "PDN_CON3"}
    for off, name in sorted(names.items()):
        log("| 0x%03x | %s | %s |" % (off, fmt(f.peek(CONFG + off)), name))
    log("")

    def dump_blocks(title):
        log("### %s" % title)
        log("")
        log("| block | +0x0 | +0x4 | +0x8 | +0xc | +0x10 | +0x14 |")
        log("|---|---|---|---|---|---|---|")
        for name, base in BLOCKS.items():
            vals = [fmt(f.peek(base + o)) for o in range(0, 0x18, 4)]
            log("| %s 0x%08x | %s |" % (name, base, " | ".join(vals)))
        log("")

    dump_blocks("Modem blocks before")

    # Clear power-down for TDMA, BSI, BPI, AFC, APC (leave RTC bit1 alone)
    log("### Writing PDN_CLR2 (0xa0010328) = 0x3d")
    log("")
    log("```")
    log(f.command("poke 0xa0010328 0x3d")[0].decode(errors="replace").strip())
    log("```")
    log("")
    log("PDN_CON2 after: %s" % fmt(f.peek(CONFG + 0x308)))
    log("")

    dump_blocks("Modem blocks after clearing power-down")

    log("### TQCNT (0x83020000) sampled 10 times, ~0.1 s apart")
    log("")
    samples = []
    for _ in range(10):
        samples.append(fmt(f.peek(TDMA)))
        time.sleep(0.1)
    log(" ".join(samples))
    log("")
    changing = len(set(samples)) > 1
    log("Counter %s." % ("is running" if changing else "did not change"))
    log("")

    if out:
        with open(out, "a") as fh:
            fh.write("\n".join(lines) + "\n")
        print("appended to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
