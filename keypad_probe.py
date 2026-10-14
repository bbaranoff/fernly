#!/usr/bin/env python3
"""Watch the Fernvale keypad registers through fernly while keys are pressed.
Prints every change of KBD_MEM1/2/3 (0xa00d0004/8/c) and the KP status word
for SECONDS, first as is, then after powering the KP block (PDN_CON1 bit 1).
Usage: keypad_probe.py PORT [SECONDS]"""
import sys, time
sys.path.insert(0, __file__.rsplit("/", 1)[0] or ".")
from regscan import Fernly, fmt

def watch(f, seconds, label):
    print("--- %s: press keys now (%d s)" % (label, seconds), flush=True)
    last = None; end = time.time() + seconds; changes = 0
    while time.time() < end:
        cur = tuple(f.peek(0xa00d0000 + o) for o in (0, 4, 8, 12))
        if cur != last:
            print("  %s" % " ".join(fmt(v) for v in cur), flush=True); last = cur; changes += 1
    print("  %d distinct states" % changes)

f = Fernly(sys.argv[1], timeout=3); assert f.sync()
secs = int(sys.argv[2]) if len(sys.argv) > 2 else 15
print("PDN_CON1 =", fmt(f.peek(0xa0010304)))
watch(f, secs, "KP as left by fernly")
f.command("poke 0xa0010324 0x2")          # PDN_CON1_CLR bit 1 (KP on MT6235 layout)
print("PDN_CON1 after clearing bit 1 =", fmt(f.peek(0xa0010304)))
watch(f, secs, "KP block powered")
