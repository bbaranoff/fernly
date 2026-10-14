#!/usr/bin/env python3
"""Second TDMA probe: dump the PLL and CONFG blocks word by word, then try
a few ways of getting the TDMA quarter-bit counter (0x83020000) to run.

Experiments, in order (each followed by 5 TQCNT samples):
  1. rewrite WRAP / WRAPIMD (0x83020004/8) with 4999
  2. clear every PDN_CON2 bit except RTC (PDN_CLR2 = 0xfffd)
  3. poke candidate DPLL_CON0 registers in the PLL block (0xa01700c0,
     0xa01701c0) with 1 if they read back as writable

Usage: tdma_probe2.py PORT [OUT.md]
"""

import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0] or ".")
from regscan import Fernly, fmt  # noqa: E402

TDMA = 0x83020000
PLL = 0xa0170000
CONFG = 0xa0010000


def main():
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    f = Fernly(port, timeout=3)
    if not f.sync():
        print("no prompt")
        return 1
    lines = ["## TDMA probe 2: PLL/CONFG word dump and wake-up attempts", ""]

    def log(s=""):
        print(s, flush=True)
        lines.append(s)

    def dump(base, size, title):
        log("### %s (0x%08x, %d bytes, non-zero words only)" % (title, base, size))
        log()
        log("| offset | value |")
        log("|---|---|")
        for off in range(0, size, 4):
            v = f.peek(base + off)
            if v != 0:
                log("| 0x%03x | %s |" % (off, fmt(v)))
        log()

    def samples(label):
        s = [fmt(f.peek(TDMA)) for _ in range(5)]
        log("TQCNT after %s: %s -> %s" % (label, " ".join(s),
            "RUNNING" if len(set(s)) > 1 else "stopped"))
        log()
        return len(set(s)) > 1

    dump(PLL, 0x800, "PLL block")
    dump(CONFG, 0x400, "CONFG block")

    log("### Experiment 1: rewrite WRAP/WRAPIMD = 4999")
    log()
    f.command("poke 0x83020004 0x1387")
    f.command("poke 0x83020008 0x1387")
    if samples("WRAP rewrite"):
        return finish(lines, out)

    log("### Experiment 2: PDN_CLR2 = 0xfffd (everything but RTC)")
    log()
    f.command("poke 0xa0010328 0xfffd")
    log("PDN_CON2 now %s" % fmt(f.peek(CONFG + 0x308)))
    if samples("PDN_CLR2=0xfffd"):
        return finish(lines, out)

    log("### Experiment 3: candidate DPLL_CON0 registers")
    log()
    for cand in (PLL + 0x0c0, PLL + 0x1c0):
        before = f.peek(cand)
        f.command("poke 0x%08x 0x1" % cand)
        after = f.peek(cand)
        log("0x%08x: before %s, after writing 1: %s" % (cand, fmt(before), fmt(after)))
        time.sleep(0.05)
        if samples("poke 0x%08x" % cand):
            return finish(lines, out)

    log("Counter never started in this probe.")
    return finish(lines, out)


def finish(lines, out):
    if out:
        with open(out, "a") as fh:
            fh.write("\n".join(lines) + "\n")
        print("appended to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
