#!/usr/bin/env python3
"""Extract an RDP command capture from a debug log for offline validation.

Usage:
    python tools/rdp_log_to_hex.py capture.log frame.rdp [--raw] [--index N]
    libdragon exec rdpvalidate -d frame.rdp        # validate + disassemble

The engine's capture (Debug tab > RDP Log > Capture!) prints lines such as
    [0xa00eecc0] e41e42dc011c82b8    TEX_RECT  tile=1 ...
between RDPLOG_BEGIN and RDPLOG_END. The trace starts part-way through the
first frame, so the capture spans two frames; by default this keeps only the
complete frame (from one SET_COLOR_IMAGE to the next, or to the end). The
64-bit command words are written one per line in the "0x..." hex format
rdpvalidate reads. With several captures in the log, the last one is used
(or --index N, 0-based). --raw keeps every captured word.
"""
import argparse
import re
import sys

WORD = re.compile(r"^\[0x[0-9a-fA-F]+\]\s+([0-9a-fA-F]{16})\b(.*)$")


def read_text(path):
    """Read a capture as text: UTF-8, or UTF-16 as written by Windows
    PowerShell 5.1 redirection (`>`, Tee-Object, Out-File)."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return data.decode("utf-16", errors="replace")
    return data.decode("utf-8-sig", errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("out")
    ap.add_argument("--index", type=int, default=-1, help="which capture to extract (default: last)")
    ap.add_argument("--raw", action="store_true", help="keep everything, do not trim to a complete frame")
    args = ap.parse_args()

    captures, cur = [], None
    for line in read_text(args.log).splitlines():
        line = line.strip()
        if line.startswith("RDPLOG_BEGIN"):
            cur = []
        elif line.startswith("RDPLOG_END"):
            if cur is not None:
                captures.append(cur)
            cur = None
        elif cur is not None:
            m = WORD.match(line)
            if m:
                cur.append((m.group(1), m.group(2)))

    if not captures:
        print("error: no RDPLOG_BEGIN/RDPLOG_END capture found", file=sys.stderr)
        return 2
    cap = captures[args.index]
    total = len(cap)

    # Keep one complete frame: from a SET_COLOR_IMAGE (rdpq_attach) to the next
    starts = [i for i, (_, dis) in enumerate(cap) if "SET_COLOR_IMAGE" in dis]
    # rdpq_attach() sends SET_Z_IMAGE just before SET_COLOR_IMAGE: include it,
    # otherwise the frame looks like it uses a Z-buffer that was never set.
    def frame_start(i):
        return i - 1 if i > 0 and "SET_Z_IMAGE" in cap[i - 1][1] else i
    if not args.raw:
        if len(starts) >= 2:
            cap = cap[frame_start(starts[0]):frame_start(starts[1])]
        elif starts:
            cap = cap[frame_start(starts[0]):]

    with open(args.out, "w", encoding="ascii") as out:
        out.write(f"# RDP capture {args.index % len(captures)} of {len(captures)} from {args.log}\n")
        for w, _ in cap:
            out.write(f"0x{w.upper()}\n")
    print(f"wrote {len(cap)} of {total} captured RDP command words to {args.out} "
          f"({len(starts)} SET_COLOR_IMAGE in capture{', raw' if args.raw else ''})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
