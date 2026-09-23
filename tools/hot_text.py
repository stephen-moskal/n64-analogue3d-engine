#!/usr/bin/env python3
"""Check the hot-text layout of an engine ELF (ROADMAP_v2 D25).

The VR4300 instruction cache is 16 KB, direct-mapped, with 32-byte lines: two
code addresses that are equal modulo 16 KB share a cache line and evict each
other. Code that runs interleaved, such as a triangle loop and
rdpq_triangle_rsp, must therefore occupy disjoint line indexes, or every
triangle pays for refetching both (~1,100 cycles measured on the A3D).

src/engine/hot_text.ld links the render path contiguously at the start of
.text. This tool checks, for each render phase (the code that runs interleaved
while one kind of geometry is drawn):

  * every phase function exists and lies inside the hot block;
  * no two functions of the phase share an I-cache line;

and lists the phase roots' direct callees that live outside the block (each of
those can evict hot code while the loop runs).

Usage:
    python3 tools/hot_text.py build/debug/engine-debug.elf [--verbose]

Needs mips64-elf-nm / mips64-elf-objdump ($N64_INST/bin or PATH). Exits 1 on a
violation.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ICACHE_BYTES = 16 * 1024
LINE_BYTES = 32

# Every rdpq_triangle() runs through these (rdpq_triangle_rsp calls floorf and floor)
TRI_PATH = ["rdpq_triangle", "rdpq_triangle_rsp", "floorf", "floor"]
# Per group / per draw inside a phase: prim colour and mode changes
GROUP_PATH = ["__rdpq_write8_syncchange", "__rdpq_fixup_write8_syncchange",
              "__rdpq_fixup_mode", "__rdpq_fixup_mode3", "__rdpq_fixup_mode4",
              "rdpq_set_mode_standard"]

# Phase roots. Their direct callees inside the hot block join the phase
# automatically. A trailing '?' marks an optional root (skipped when absent).
PHASES = {
    "mesh":        ["mesh_draw"],
    "mesh_legacy": ["mesh_draw_legacy?"],      # temporary S2 A/B copy
    "floor":       ["floor_draw"],
    "shadow":      ["shadow_begin", "shadow_draw_blob", "shadow_draw_projected"],
    "particle":    ["particle_draw"],
}

# Callees that are allowed outside the block: rare (buffer switch, asserts)
# or per-group work too large to pin (texture uploads).
COLD_OK = {"rspq_next_buffer", "texture_upload", "__assert_func", "debugf",
           "assertf", "__rdpq_debug_log"}


def tool(name):
    path = os.path.join(os.environ.get("N64_INST", ""), "bin", name)
    if os.path.exists(path):
        return path
    return shutil.which(name) or ""


def read_symbols(elf):
    nm = tool("mips64-elf-nm")
    if not nm:
        raise RuntimeError("mips64-elf-nm not found (set N64_INST or run inside the libdragon container)")
    out = subprocess.run([nm, "-n", "-S", elf], check=True, capture_output=True, text=True).stdout
    funcs, marks = {}, {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[2] in ("T", "t"):
            funcs.setdefault(parts[3], (int(parts[0], 16), int(parts[1], 16)))
        elif len(parts) == 3 and parts[2] in ("__engine_hot_start", "__engine_hot_end"):
            marks[parts[2]] = int(parts[0], 16)
    return funcs, marks


CALL_RE = re.compile(r"\bj(?:al)?\s+[0-9a-f]+ <([^>+]+)>")


def direct_callees(elf, start, size):
    objdump = tool("mips64-elf-objdump")
    out = subprocess.run([objdump, "-d", "--no-show-raw-insn",
                          f"--start-address={start:#x}", f"--stop-address={start + size:#x}", elf],
                         check=True, capture_output=True, text=True).stdout
    return {m.group(1) for m in CALL_RE.finditer(out)}


N_LINES = ICACHE_BYTES // LINE_BYTES


def lines_of(addr, size):
    """I-cache index -> memory line number, for the lines a function occupies."""
    first = addr // LINE_BYTES
    last = (addr + size - 1) // LINE_BYTES
    return {line % N_LINES: line for line in range(first, last + 1)}


def collide(a, b):
    """Number of cache indexes where the two functions need different lines
    (adjacent functions sharing one line is not a collision)."""
    return sum(1 for idx, line in b.items() if idx in a and a[idx] != line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    try:
        funcs, marks = read_symbols(args.elf)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    if "__engine_hot_start" not in marks or "__engine_hot_end" not in marks:
        print("hot text: FAIL, no __engine_hot_start/__engine_hot_end (engine.ld not used?)")
        return 1
    hot_lo, hot_hi = marks["__engine_hot_start"], marks["__engine_hot_end"]

    def inside(name):
        a, s = funcs[name]
        return hot_lo <= a and a + s <= hot_hi

    errors, notes, summary = [], [], []
    for phase, roots in PHASES.items():
        present = []
        for r in roots:
            name = r.rstrip("?")
            if name in funcs:
                present.append(name)
            elif not r.endswith("?"):
                errors.append(f"{phase}: root {name} not found")
        if not present:
            continue

        members = set(present) | set(TRI_PATH) | set(GROUP_PATH)
        outside = set()
        for r in present:
            a, s = funcs[r]
            for c in direct_callees(args.elf, a, s):
                if c not in funcs or c in members:
                    continue
                if inside(c):
                    members.add(c)
                elif c not in COLD_OK:
                    outside.add(c)

        for m in sorted(members):
            if m not in funcs:
                errors.append(f"{phase}: {m} not found")
            elif not inside(m):
                errors.append(f"{phase}: {m} at {funcs[m][0]:#x} is outside the hot block")

        # Pairwise I-cache line collisions within the phase
        ranges = [(m, funcs[m]) for m in sorted(members) if m in funcs]
        collisions = []
        for i in range(len(ranges)):
            li = lines_of(*ranges[i][1])
            for j in range(i + 1, len(ranges)):
                shared = collide(li, lines_of(*ranges[j][1]))
                if shared:
                    collisions.append(f"{ranges[i][0]} x {ranges[j][0]} ({shared} lines)")
        for c in collisions:
            errors.append(f"{phase}: I-cache collision {c}")
        if outside:
            notes.append(f"{phase}: callees outside the block: {', '.join(sorted(outside))}")
        size = sum(s for _, (_, s) in ranges)
        summary.append(f"{phase} {size / 1024:.1f} KB {'OK' if not collisions else 'COLLIDES'}")
        if args.verbose:
            for m, (a, s) in ranges:
                print(f"  {phase:12s} {a:#010x} {s:6d}  {m}")

    block = hot_hi - hot_lo
    print(f"hot text {block / 1024:.1f} KB at {hot_lo:#x} | " + " | ".join(summary))
    for n in notes:
        print(f"  note: {n}")
    for e in errors:
        print(f"  FAIL: {e}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
