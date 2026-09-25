#!/usr/bin/env python3
"""Extract an RDP command capture from a debug log for offline validation.

Usage:
    python tools/rdp_log_to_hex.py capture.log frame.rdp [--raw] [--index N]
    python tools/rdp_log_to_hex.py capture.log outdir --tagged
    libdragon exec rdpvalidate -d frame.rdp        # validate + disassemble

The engine's capture (Debug tab > RDP Log > Capture!) prints lines such as
    [0xa00eecc0] e41e42dc011c82b8    TEX_RECT  tile=1 ...
between RDPLOG_BEGIN and RDPLOG_END. The trace starts part-way through the
first frame, so the capture spans two frames; by default this keeps only the
complete frame (from one SET_COLOR_IMAGE to the next, or to the end). The
64-bit command words are written one per line in the "0x..." hex format
rdpvalidate reads. With several captures in the log, the last one is used
(or --index N, 0-based). --raw keeps every captured word.

--tagged writes every capture of a benchmark ROM built with BENCH_RDPLOG=1
(one per step, each after a "BENCH_RDPLOG,<kind>,<index>,<param>" row) to
outdir/<kind>-<param>.rdp, plus outdir/manifest.csv: per step the triangle
count per command and a SHA-256 of the triangle commands alone. Triangles
are what the engine's geometry path produces; the rest of the frame holds
buffer addresses that move between builds and HUD text that changes with
timing. Two builds that draw the same triangles have equal hashes (the
vertex cache's identity check, ROADMAP_v2 Phase 3 S3):
    python tools/rdp_log_to_hex.py ares.log build/rdplog --tagged \\
        --golden docs/benchmarks/2026-09-25-p3-s1-mesh-rdplog-ares.csv
"""
import argparse
import hashlib
import os
import re
import sys

WORD = re.compile(r"^\[0x[0-9a-fA-F]+\]\s+([0-9a-fA-F]{16})\b(.*)$")
TAG = re.compile(r"BENCH_RDPLOG,([^,]+),(\d+),(-?\d+)")

# Command lengths in 64-bit words (libdragon rdpq_debug_disasm_size)
TRI_WORDS = {0x08: 4, 0x09: 6, 0x0A: 12, 0x0B: 14, 0x0C: 12, 0x0D: 14, 0x0E: 20, 0x0F: 22}
TRI_NAMES = {0x08: "tri", 0x09: "tri_z", 0x0A: "tri_tex", 0x0B: "tri_tex_z", 0x0C: "tri_shade",
             0x0D: "tri_shade_z", 0x0E: "tri_tex_shade", 0x0F: "tri_tex_shade_z"}


def read_text(path):
    """Read a capture as text: UTF-8, or UTF-16 as written by Windows
    PowerShell 5.1 redirection (`>`, Tee-Object, Out-File)."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return data.decode("utf-16", errors="replace")
    return data.decode("utf-8-sig", errors="replace")


def read_captures(path):
    """Every capture in the log as (tag, [(word, disassembly)]); tag is the
    last BENCH_RDPLOG row before its RDPLOG_BEGIN, or None."""
    captures, cur, tag, cur_tag = [], None, None, None
    for line in read_text(path).splitlines():
        line = line.strip()
        t = TAG.search(line)
        if t:
            tag = (t.group(1), int(t.group(3)))
        elif line.startswith("RDPLOG_BEGIN"):
            cur, cur_tag, tag = [], tag, None
        elif line.startswith("RDPLOG_END"):
            if cur is not None:
                captures.append((cur_tag, cur))
            cur = None
        elif cur is not None:
            m = WORD.match(line)
            if m:
                cur.append((m.group(1), m.group(2)))
    return captures


def complete_frame(cap):
    """One complete frame: from a SET_COLOR_IMAGE (rdpq_attach) to the next.
    Returns (words, number of SET_COLOR_IMAGE in the capture)."""
    starts = [i for i, (_, dis) in enumerate(cap) if "SET_COLOR_IMAGE" in dis]
    # rdpq_attach() sends SET_Z_IMAGE just before SET_COLOR_IMAGE: include it,
    # otherwise the frame looks like it uses a Z-buffer that was never set.
    def frame_start(i):
        return i - 1 if i > 0 and "SET_Z_IMAGE" in cap[i - 1][1] else i
    if len(starts) >= 2:
        return cap[frame_start(starts[0]):frame_start(starts[1])], len(starts)
    if starts:
        return cap[frame_start(starts[0]):], len(starts)
    return cap, 0


def write_hex(path, cap, header):
    with open(path, "w", encoding="ascii") as out:
        out.write(f"# {header}\n")
        for w, _ in cap:
            out.write(f"0x{w.upper()}\n")


def triangles(cap):
    """(count per triangle command, SHA-256 of the triangle commands' words)"""
    words = [int(w, 16) for w, _ in cap]
    counts, sha, i = {}, hashlib.sha256(), 0
    while i < len(words):
        cmd = (words[i] >> 56) & 0x3F
        n = TRI_WORDS.get(cmd, 2 if cmd in (0x24, 0x25) else 1)
        if cmd in TRI_WORDS:
            counts[cmd] = counts.get(cmd, 0) + 1
            for w in words[i:i + n]:
                sha.update(w.to_bytes(8, "big"))
        i += n
    return counts, sha.hexdigest()


def read_manifest(path):
    """{(kind, param): row} of a manifest.csv; the triangle columns and hash
    are compared, not the word count (HUD text varies)."""
    with open(path, encoding="ascii") as fh:
        lines = [l.strip() for l in fh if l.strip() and not l.startswith("#")]
    head = lines[0].split(",")
    rows = {}
    for line in lines[1:]:
        row = dict(zip(head, line.split(",")))
        rows[(row["kind"], row["param"])] = row
    return rows


def compare(golden_path, rows):
    golden = read_manifest(golden_path)
    keys = ["tris"] + [TRI_NAMES[c] for c in sorted(TRI_NAMES)] + ["tri_sha256"]
    same = bad = 0
    for key, row in rows.items():
        g = golden.get(key)
        if g is None:
            print(f"  {key[0]} {key[1]}: not in the golden set")
            continue
        diff = [k for k in keys if g[k] != row[k]]
        if diff:
            bad += 1
            print(f"  {key[0]} {key[1]}: DIFFERS in {', '.join(diff)} "
                  f"(tris {g['tris']} -> {row['tris']})")
        else:
            same += 1
            print(f"  {key[0]} {key[1]}: identical ({row['tris']} triangles)")
    missing = [k for k in golden if k not in rows]
    for k in missing:
        print(f"  {k[0]} {k[1]}: missing from this log")
    print(f"{same} of {len(golden)} golden steps identical, {bad} differ, {len(missing)} missing "
          f"({golden_path})")
    return 1 if bad or missing else 0


def tagged(args, captures):
    os.makedirs(args.out, exist_ok=True)
    head = "kind,param,words,tris," + ",".join(TRI_NAMES[c] for c in sorted(TRI_NAMES)) + ",tri_sha256"
    lines, rows = [head], {}
    for n, (tag, cap) in enumerate(captures):
        name = f"{tag[0]}-{tag[1]}" if tag else f"capture-{n}"
        frame, _ = complete_frame(cap)
        write_hex(os.path.join(args.out, name + ".rdp"), frame, f"RDP capture {name} from {args.log}")
        counts, digest = triangles(frame)
        kind, param = (tag[0], str(tag[1])) if tag else ("", str(n))
        line = (f"{kind},{param},{len(frame)},{sum(counts.values())},"
                + ",".join(str(counts.get(c, 0)) for c in sorted(TRI_NAMES)) + f",{digest}")
        lines.append(line)
        rows[(kind, param)] = dict(zip(head.split(","), line.split(",")))
    with open(os.path.join(args.out, "manifest.csv"), "w", encoding="ascii") as out:
        out.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"wrote {len(captures)} captures and manifest.csv to {args.out}")
    return compare(args.golden, rows) if args.golden else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("out", help="output .rdp file (a directory with --tagged)")
    ap.add_argument("--index", type=int, default=-1, help="which capture to extract (default: last)")
    ap.add_argument("--raw", action="store_true", help="keep everything, do not trim to a complete frame")
    ap.add_argument("--tagged", action="store_true",
                    help="write every capture, named by its BENCH_RDPLOG row, and a manifest")
    ap.add_argument("--golden", metavar="MANIFEST",
                    help="with --tagged: compare the triangles with a golden manifest (exit 1 if any differ)")
    args = ap.parse_args()
    if args.golden and not args.tagged:
        ap.error("--golden needs --tagged")

    captures = read_captures(args.log)
    if not captures:
        print("error: no RDPLOG_BEGIN/RDPLOG_END capture found", file=sys.stderr)
        return 2
    if args.tagged:
        return tagged(args, captures)

    cap = captures[args.index][1]
    total = len(cap)
    frame, starts = complete_frame(cap)
    if not args.raw:
        cap = frame
    write_hex(args.out, cap, f"RDP capture {args.index % len(captures)} of {len(captures)} from {args.log}")
    print(f"wrote {len(cap)} of {total} captured RDP command words to {args.out} "
          f"({starts} SET_COLOR_IMAGE in capture{', raw' if args.raw else ''})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
