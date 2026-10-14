#!/usr/bin/env python3
"""De-replicate a byte dump of an MT6260 DSP memory window and try the C54x
disassembler from c54x_exe on it.

Each 32-bit word of the ARM view holds one 16-bit DSP word twice (bytes 0-1 ==
bytes 2-3), so the DSP word stream is bytes 0-1 of every 4-byte group.  The
unique window is PAGE bytes long (default 0x8000, i.e. 8 Ki words) and the
ARM-side page index is address bits 16..19.

Usage: dsp_disas.py DUMP.bin [--page N] [--words W] [--objdump PATH]
Writes DUMP.pageN.words (packed little-endian 16-bit) and prints a short
disassembly plus a crude plausibility score (fraction of lines objdump could
decode as something other than raw data).
"""

import argparse
import os
import subprocess
import sys

DEFAULT_OBJDUMP = "/opt/GSM/c54x_exe/build-tic54x/binutils/objdump"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--page", type=int, default=0)
    ap.add_argument("--page-size", type=lambda x: int(x, 0), default=0x8000)
    ap.add_argument("--words", type=int, default=64, help="words to show")
    ap.add_argument("--objdump", default=DEFAULT_OBJDUMP)
    a = ap.parse_args()

    raw = open(a.dump, "rb").read()
    start = a.page * 0x10000
    chunk = raw[start:start + a.page_size]
    if not chunk:
        print("page out of range")
        return 1
    replicated = sum(1 for i in range(0, len(chunk) - 3, 4) if chunk[i:i + 2] == chunk[i + 2:i + 4])
    total = len(chunk) // 4
    words = b"".join(chunk[i:i + 2] for i in range(0, len(chunk) - 1, 4))
    out = "%s.page%d.words" % (a.dump, a.page)
    open(out, "wb").write(words)
    print("page %d: %d/%d words have replicated halves; %d DSP words -> %s"
          % (a.page, replicated, total, len(words) // 2, out))

    # Quick content stats
    w16 = [int.from_bytes(words[i:i + 2], "little") for i in range(0, len(words), 2)]
    distinct = len(set(w16))
    zeros = w16.count(0)
    print("distinct words: %d, zero words: %d, first 16: %s"
          % (distinct, zeros, " ".join("%04x" % v for v in w16[:16])))

    if not os.path.exists(a.objdump):
        print("no tic54x objdump at", a.objdump)
        return 0
    res = subprocess.run([a.objdump, "-D", "-b", "binary", "-m", "tic54x",
                          "--stop-address=0x%x" % a.words, out],
                         capture_output=True, text=True)
    lines = [l for l in res.stdout.splitlines() if l and l[0] == " "]
    decoded = sum(1 for l in lines if "\t" in l and l.split("\t")[-1].strip() and
                  not l.split("\t")[-1].strip().startswith(".word"))
    print("tic54x objdump decoded %d of %d lines as instructions (raw data shows as bare words)"
          % (decoded, len(lines)))
    print("\n".join(lines[:40]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
