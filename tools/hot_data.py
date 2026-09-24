#!/usr/bin/env python3
"""Check the D-cache placement of the render path's static data (ROADMAP_v2 D34).

The VR4300 data cache is 8 KB, direct-mapped, with 16-byte lines: two
addresses that are equal modulo 8 KB ("the same colour") share a line and
evict each other. The stack sits at the top of RDRAM, so its colours are
fixed; static data follows the code, so its colours move whenever code
changes size. When a variable the triangle loop touches lands on a colour the
loop's own stack frames use, every triangle refetches both (D34: libdragon's
rspq_cur_pointer on mesh_draw's frame cost mesh_tris 14 %).

For each render phase of tools/hot_text.py (the functions a drawing loop runs
through), this tool:
  * decodes every static-data access of the phase's functions ($gp-relative,
    and lui + offset pairs) and maps it to a data symbol;
  * computes the phase's stack range from the frame sizes along its call chain
    (the scene callbacks are reached through function pointers, so their
    chains are declared in CHAINS below);
  * checks the pinned groups of src/engine/hot_data.ld: each sits at its
    colour and holds its symbols, no pinned symbol shares a line with the stack
    range of a phase that uses it, and no two pinned symbols a phase uses share
    a set on different lines;
  * notes any other static data a phase touches that shares the stack's lines.

Usage:
    python3 tools/hot_data.py build/debug/engine-debug.elf [--verbose]

--verbose lists every data symbol each phase touches, with its colours.
Exits 1 on a violation. Needs mips64-elf-nm / mips64-elf-objdump.
"""
import argparse
import bisect
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hot_text  # noqa: E402  (phases, symbol reading)

DCACHE_BYTES = 8 * 1024
DLINE = 16
N_SETS = DCACHE_BYTES // DLINE

# Stack model. The stack grows down from the top of RDRAM (0x80800000 with the
# Expansion Pak, 0x80400000 without: the same colours). ENTRY_BYTES is what
# libdragon's entry code uses above main's frame, calibrated on the A3D from a
# BENCH_LAYOUT row: bench_draw's frame at 0x807ffc80 with main 144 +
# engine_run 264 + scene_manager_draw 88 + bench_draw 280 bytes of frames.
STACK_TOP = 0x80800000
ENTRY_BYTES = 120
UPPER = ["main", "engine_run", "scene_manager_draw"]
# Frames between scene_manager_draw and each phase root, one list per scene
# (scene callbacks and object callbacks are function pointers)
CHAINS = {
    "mesh":     [["bench_draw"], ["scene_draw_objects"]],
    "floor":    [["bench_draw"], ["demo_draw"]],
    "shadow":   [["bench_draw"], ["demo_draw"]],
    "particle": [["bench_post_draw"], ["demo_post_draw"]],
}

# Static data the phases reach through pointers, which the disassembly scan
# cannot attribute (D35): every renderer gets the frame's camera and lighting
# from scene_view_camera() / scene_view_light(). The particle renderer is left
# out: it copies the matrix to its stack once per call, because the copies
# share colours with the particle pool.
REACHED = {
    "mesh":   ["view_camera", "view_light"],
    "floor":  ["view_camera", "view_light"],
    "shadow": ["view_camera", "view_light"],
}

# Pinned groups of src/engine/hot_data.ld: (start symbol, end symbol, colour,
# the data symbols the group must hold)
GROUPS = [
    ("__engine_hot_bss_start", "__engine_hot_bss_common_end", 0x0000,
     ["rspq_cur_pointer", "rspq_cur_sentinel", "rspq_block", "rspq_ctx", "rdpq_tracking",
      "rdpq_config", "__rdpq_inited", "rdpq_block_state", "g_prof_on", "g_prof_calls",
      "g_prof_ticks", "g_stats_cur", "particle_initialized", "particle_pool_allocated"]),
    ("__engine_hot_rodata_start", "__engine_hot_rodata_end", 0x0200,
     ["TRIFMT_ZBUF", "TRIFMT_ZBUF_TEX", "TRIFMT_ZBUF_SHADE", "TRIFMT_ZBUF_SHADE_TEX", "g_fog",
      "view_camera", "view_light"]),
    ("__engine_hot_bss_floor", "__engine_hot_bss_shadow", 0x0700, ["grid_valid", "grid_depth", "grid"]),
    ("__engine_hot_bss_shadow", "__engine_hot_bss_particle", 0x0F20, ["shadow_state", "shadow_scr"]),
    ("__engine_hot_bss_particle", "__engine_hot_bss_end", 0x0300, ["particle_pool"]),
]
MARKS = {g[0] for g in GROUPS} | {g[1] for g in GROUPS}
# Arrays of which only a prefix is hot: the shadow scratch is indexed by vertex,
# so a caster touches its first vertex_count entries (the demo's largest caster,
# the 6x6 sphere, has fewer than 64; 128 leaves room)
HOT_PREFIX = {"shadow_scr": 128 * 12, "shadow_state": 128}

DATA_TYPES = set("bBdDrRsSgGvV")
INSN_RE = re.compile(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$")
MEM_RE = re.compile(r"^(-?\d+)\((\w+)\)$")
CLOBBERED_BY_CALL = {"at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3",
                     "t4", "t5", "t6", "t7", "t8", "t9", "ra"}
LOADS = {"lb", "lbu", "lh", "lhu", "lw", "lwu", "ld", "ll", "lwl", "lwr"}


def colour(addr):
    return addr & (DCACHE_BYTES - 1) & ~(DLINE - 1)


def sets_of(lo, hi):
    """D-cache set indexes of the lines covering [lo, hi)."""
    return {(line % N_SETS) for line in range(lo // DLINE, (hi - 1) // DLINE + 1)} if hi > lo else set()


def read_data_symbols(elf):
    nm = hot_text.tool("mips64-elf-nm")
    out = subprocess.run([nm, "-n", "-S", elf], check=True, capture_output=True, text=True).stdout
    syms, marks = [], {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[2] in DATA_TYPES:
            syms.append((int(p[0], 16), int(p[1], 16), p[3]))
        elif len(p) >= 3 and p[-1] in ("_gp", *MARKS):
            marks[p[-1]] = int(p[0], 16)
    syms.sort()
    return syms, marks


def disasm(elf, start, size):
    objdump = hot_text.tool("mips64-elf-objdump")
    return subprocess.run([objdump, "-d", "--no-show-raw-insn",
                           f"--start-address={start:#x}", f"--stop-address={start + size:#x}", elf],
                          check=True, capture_output=True, text=True).stdout


def frame_size(text):
    """Bytes the function's prologue reserves (addiu sp,sp,-N), 0 for a leaf."""
    for n, line in enumerate(text.splitlines()):
        m = INSN_RE.match(line)
        if not m:
            continue
        if m.group(2) in ("addiu", "daddiu") and m.group(3).startswith("sp,sp,-"):
            return int(m.group(3).split(",")[2].lstrip("-"))
        if n > 40:
            break
    return 0


def data_refs(text, gp):
    """Absolute addresses the code reads, writes or takes the address of."""
    val, refs = {}, set()
    for line in text.splitlines():
        m = INSN_RE.match(line)
        if not m:
            continue
        op, args = m.group(2), [a.strip() for a in m.group(3).split(",")] if m.group(3) else []
        if op in ("jal", "jalr", "bal"):
            for r in CLOBBERED_BY_CALL:
                val.pop(r, None)
            continue
        if op == "lui" and len(args) == 2:
            val[args[0]] = (int(args[1], 16) << 16) & 0xFFFFFFFF
            continue
        if op in ("addiu", "daddiu") and len(args) == 3:
            base = gp if args[1] == "gp" else val.get(args[1])
            if base is not None:
                addr = (base + int(args[2])) & 0xFFFFFFFF
                refs.add(addr)
                val[args[0]] = addr
            else:
                val.pop(args[0], None)
            continue
        if op == "move" and len(args) == 2:
            if args[1] in val:
                val[args[0]] = val[args[1]]
            else:
                val.pop(args[0], None)
            continue
        if len(args) == 2:
            mm = MEM_RE.match(args[1])
            if mm:
                base = gp if mm.group(2) == "gp" else val.get(mm.group(2))
                if base is not None:
                    refs.add((base + int(mm.group(1))) & 0xFFFFFFFF)
                if op in LOADS:
                    val.pop(args[0], None)
                continue
        if args and not args[0].startswith("$f"):
            val.pop(args[0], None)   # any other write to a GPR
    return refs


def symbol_at(syms, starts, addr):
    i = bisect.bisect_right(starts, addr) - 1
    if i >= 0:
        a, s, name = syms[i]
        if a <= addr < a + max(s, 1):
            return syms[i]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--report", action="store_true", help="report only, never fail")
    args = ap.parse_args()

    try:
        funcs, tmarks = hot_text.read_symbols(args.elf)
        syms, marks = read_data_symbols(args.elf)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    if "_gp" not in marks:
        print("hot data: FAIL, no _gp symbol")
        return 1
    gp = marks["_gp"]
    starts = [s[0] for s in syms]
    by_name = {s[2]: s for s in syms}
    phases = hot_text.phase_members(args.elf, funcs, tmarks)

    texts = {}

    def text_of(f):
        if f not in texts:
            a, s = funcs[f]
            texts[f] = disasm(args.elf, a, s)
        return texts[f]

    errors, notes, summary = [], [], []

    # Pinned groups: colour and contents
    pinned_where = {}
    for start, end, want, names in GROUPS:
        if start not in marks or end not in marks:
            errors.append(f"group {start} missing (hot_data.ld not linked?)")
            continue
        lo, hi = marks[start], marks[end]
        if colour(lo) != want:
            errors.append(f"{start} at {lo:#x}: colour {colour(lo):#06x}, expected {want:#06x}")
        for name in names:
            s = by_name.get(name)
            if s is None:
                notes.append(f"{name} not in this ELF")
                continue
            if not (lo <= s[0] and s[0] + s[1] <= hi):
                errors.append(f"{name} at {s[0]:#x} is outside its group {start} ({lo:#x}..{hi:#x})")
            pinned_where[name] = s

    upper = sum(frame_size(text_of(f)) for f in UPPER if f in funcs)
    caller_sp = STACK_TOP - ENTRY_BYTES - upper

    for phase, (members, _outside, _errs) in phases.items():
        if not members:
            continue

        # Deepest stack use below the phase roots, through the phase's callees
        memo = {}

        def depth(f, seen=()):
            if f in memo:
                return memo[f]
            if f not in funcs or f in seen:
                return 0
            own = frame_size(text_of(f))
            a, s = funcs[f]
            callees = [c for c in hot_text.direct_callees(args.elf, a, s) if c in members]
            memo[f] = own + max((depth(c, seen + (f,)) for c in callees), default=0)
            return memo[f]

        roots = [r.rstrip("?") for r in hot_text.PHASES[phase] if r.rstrip("?") in funcs]
        lows = []
        for chain in CHAINS.get(phase, [[]]):
            missing = [f for f in chain if f not in funcs]
            if missing:
                notes.append(f"{phase}: chain {'>'.join(chain)} skipped, missing {', '.join(missing)}")
                continue
            frames = sum(frame_size(text_of(f)) for f in chain)
            lows.append(caller_sp - frames - max(depth(r) for r in roots))
        if not lows:
            continue
        stack_lo, stack_hi = min(lows), caller_sp
        stack_sets = sets_of(stack_lo, stack_hi)

        # Static data the phase touches: named in its code, or reached
        # through the pointers the scenes pass (REACHED)
        touched = {}
        for f in members:
            if f not in funcs:
                continue
            for addr in data_refs(text_of(f), gp):
                s = symbol_at(syms, starts, addr)
                if s:
                    touched[s[2]] = s
        for name in REACHED.get(phase, []):
            if name in by_name:
                touched[name] = by_name[name]
            else:
                errors.append(f"{phase}: {name} (REACHED) not in this ELF")
        clashes = []
        for name, (a, size, _) in sorted(touched.items(), key=lambda kv: kv[1][0]):
            size = min(size, HOT_PREFIX.get(name, size))
            shared = sets_of(a, a + max(size, 1)) & stack_sets
            if shared:
                clashes.append((name, len(shared)))
                if name in pinned_where:
                    errors.append(f"{phase}: pinned {name} shares {len(shared)} D-cache lines with the stack")
            if args.verbose:
                flag = f"  <-- {len(shared)} lines on the stack" if shared else ""
                pin = " [pinned]" if name in pinned_where else ""
                print(f"  {phase:9s} {a:#010x} {size:6d} colour {colour(a):#06x}..{colour(a + max(size, 1) - 1):#06x}"
                      f"  {name}{pin}{flag}")
        # Pinned symbols of this phase must not evict each other
        lines_by_set = {}
        for name in sorted(n for n in touched if n in pinned_where):
            a, size, _ = touched[name]
            size = min(size, HOT_PREFIX.get(name, size))
            for line in range(a // DLINE, (a + max(size, 1) - 1) // DLINE + 1):
                other = lines_by_set.setdefault(line % N_SETS, (line, name))
                if other[0] != line:
                    errors.append(f"{phase}: pinned {name} and {other[1]} share D-cache set "
                                  f"{(line % N_SETS) * DLINE:#06x}")
                    break
        unpinned = [f"{n} ({k})" for n, k in clashes if n not in pinned_where]
        if unpinned:
            notes.append(f"{phase}: unpinned data on the stack's lines: {', '.join(unpinned)}")
        summary.append(f"{phase} stack {colour(stack_lo):#06x}..{colour(stack_hi - 1):#06x}, "
                       f"{len(touched)} symbols, {len(clashes)} on stack lines")

    print("hot data | " + " | ".join(summary))
    for n in notes:
        print(f"  note: {n}")
    for e in errors:
        print(f"  {'report' if args.report else 'FAIL'}: {e}")
    return 1 if errors and not args.report else 0


if __name__ == "__main__":
    sys.exit(main())
