#!/usr/bin/env python3
"""Characterise the 16-bit memory windows at 0x821xxxxx-0x823xxxxx (DSP RAM
seen from the ARM) using fernly's byte-wise "hex" dump.

For each region it saves a raw dump, then reports:
  - byte vs 32-bit view at the first words (is the half-word replicated?)
  - the smallest alias stride S such that mem[x] == mem[x+S] everywhere
  - which 512-byte chunks hold structured data vs a repeated 2-byte fill
  - whether the regions are distinct memories or views of the same one

Usage: dsp_probe.py PORT [OUT.md] [DUMPDIR]
"""

import hashlib
import os
import re
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0] or ".")
from regscan import Fernly, fmt  # noqa: E402

CHUNK = 0x200
REGIONS = [("bank A", 0x82100000, 0x80000),
           ("bank B", 0x82200000, 0x80000),
           ("bank C", 0x82300000, 0x40000)]
HEXLINE = re.compile(rb"^([0-9a-f]{8})((?: [0-9a-f]{2}){8} (?: [0-9a-f]{2}){8})  \|")


def hexdump(f, addr, count):
    out, ok = f.command("hex 0x%08x 0x%x" % (addr, count))
    if not ok:
        return None
    data = bytearray()
    for line in out.replace(b"\r", b"").split(b"\n"):
        m = HEXLINE.match(line)
        if m:
            data += bytes.fromhex(m.group(2).replace(b" ", b"").decode())
    return bytes(data) if len(data) == count else None


def is_fill(chunk):
    return len(chunk) >= 4 and chunk == chunk[:2] * (len(chunk) // 2)


def alias_stride(chunks):
    n = len(chunks)
    for s in (1 << k for k in range(0, 12)):
        if s >= n:
            return None
        if all(chunks[i] == chunks[i + s] for i in range(n - s)):
            return s * CHUNK
    return None


def main():
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    dumpdir = sys.argv[3] if len(sys.argv) > 3 else "dumps"
    os.makedirs(dumpdir, exist_ok=True)
    f = Fernly(port, timeout=5)
    if not f.sync():
        print("no prompt")
        return 1
    lines = ["## DSP memory windows (byte-wise dumps)", ""]

    def log(s=""):
        print(s, flush=True)
        lines.append(s)

    # Byte vs word view
    log("### Byte view vs 32-bit view at 0x82100000")
    log()
    log("| addr | bytes (hex, as dumped) | peek32 |")
    log("|---|---|---|")
    head = hexdump(f, 0x82100000, 16)
    for i in range(0, 16, 4):
        log("| 0x%08x | %s | %s |" % (0x82100000 + i, head[i:i + 4].hex(" "),
                                       fmt(f.peek(0x82100000 + i))))
    log()
    if head[0:2] == head[2:4] and head[4:6] == head[6:8]:
        log("Bytes 0-1 equal bytes 2-3 in every word: the half-word is replicated, "
            "i.e. a 16-bit memory behind a bridge that ignores A1.")
    else:
        log("Bytes 0-1 differ from bytes 2-3: two distinct 16-bit words per 32-bit word.")
    log()

    digests = {}
    for name, base, size in REGIONS:
        chunks = []
        for off in range(0, size, CHUNK):
            c = hexdump(f, base + off, CHUNK)
            if c is None:
                log("dump failed at 0x%08x, stopping this region" % (base + off))
                break
            chunks.append(c)
            if (off // CHUNK) % 64 == 0:
                print("  %s 0x%08x" % (name, base + off), flush=True)
        raw = b"".join(chunks)
        path = os.path.join(dumpdir, "%08x.bin" % base)
        open(path, "wb").write(raw)
        digests[name] = hashlib.sha1(raw).hexdigest()
        stride = alias_stride(chunks)
        fills = [is_fill(c) for c in chunks]
        structured = [i for i, fl in enumerate(fills) if not fl]
        log("### %s 0x%08x, %d KiB dumped to %s" % (name, base, size // 1024, path))
        log()
        log("- sha1: %s" % digests[name])
        log("- smallest alias stride: %s" % ("0x%x" % stride if stride else "none within region"))
        if stride:
            unique = chunks[: stride // CHUNK]
            ufills = fills[: stride // CHUNK]
            log("- unique window: 0x%x bytes, of which %d x %d-byte chunks are structured and %d are 2-byte fill"
                % (stride, sum(1 for x in ufills if not x), CHUNK, sum(1 for x in ufills if x)))
            runs = []
            cur = None
            for i, fl in enumerate(ufills):
                kind = "fill %s" % unique[i][:2].hex() if fl else "data"
                if cur and cur[0] == kind:
                    cur[2] = i
                else:
                    cur = [kind, i, i]
                    runs.append(cur)
            log("- layout of the unique window:")
            for kind, a, b in runs:
                log("  - 0x%05x-0x%05x: %s" % (a * CHUNK, (b + 1) * CHUNK - 1, kind))
        else:
            log("- structured chunks: %d of %d" % (len(structured), len(chunks)))
        log()

    log("### Are the regions the same memory?")
    log()
    for a in REGIONS:
        for b in REGIONS:
            if a[0] < b[0]:
                same = digests.get(a[0]) == digests.get(b[0])
                log("- %s vs %s: %s" % (a[0], b[0], "identical dumps" if same else "different"))
    log()

    if out:
        with open(out, "a") as fh:
            fh.write("\n".join(lines) + "\n")
        print("appended to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
