#!/usr/bin/env python3
"""Check ROM size and RAM footprint against budgets (ROADMAP_v2 P1.9).

Usage:
    python3 tools/rom_budget.py --rom engine.z64 --elf build/release/engine.elf \
        [--max-rom-kb 4096] [--max-ram-kb 1024]

RAM footprint = everything resident before any heap allocation: from the start
of the code (__text_start) to the start of the heap (end). That is text + data
+ bss plus the alignment pads between sections, such as the ones that pin the
hot data to fixed D-cache colours (src/engine/hot_data.ld); the pads are shown
separately. Sizes come from the toolchain's `mips64-elf-size` and
`mips64-elf-nm` (from $N64_INST/bin or PATH). Prints a one-line summary; exits
1 when a budget is exceeded.
"""
import argparse
import os
import shutil
import subprocess
import sys


def tool(name):
    path = os.path.join(os.environ.get("N64_INST", ""), "bin", name)
    if not os.path.exists(path):
        path = shutil.which(name) or ""
    if not path:
        raise RuntimeError(f"{name} not found (set N64_INST or run inside the libdragon container)")
    return path


def elf_sizes(elf):
    out = subprocess.run([tool("mips64-elf-size"), elf], check=True, capture_output=True, text=True).stdout
    # Berkeley format: "text data bss dec hex filename"
    fields = out.strip().splitlines()[-1].split()
    return int(fields[0]), int(fields[1]), int(fields[2])


def resident_span(elf):
    """Bytes from __text_start to end (the heap start), or None if absent."""
    out = subprocess.run([tool("mips64-elf-nm"), elf], check=True, capture_output=True, text=True).stdout
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] in ("__text_start", "end"):
            syms[p[2]] = int(p[0], 16)
    if len(syms) != 2:
        return None
    return syms["end"] - syms["__text_start"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--max-rom-kb", type=int, default=4096)
    ap.add_argument("--max-ram-kb", type=int, default=1024)
    args = ap.parse_args()

    try:
        rom = os.path.getsize(args.rom)
        text, data, bss = elf_sizes(args.elf)
        span = resident_span(args.elf)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    sections = text + data + bss
    ram = span if span is not None and span >= sections else sections
    pads = ram - sections
    ok_rom = rom <= args.max_rom_kb * 1024
    ok_ram = ram <= args.max_ram_kb * 1024
    print(f"ROM {rom / 1024:.1f} KB (budget {args.max_rom_kb} KB) {'OK' if ok_rom else 'OVER'} | "
          f"RAM text {text} + data {data} + bss {bss} + pads {pads} = {ram / 1024:.1f} KB "
          f"(budget {args.max_ram_kb} KB) {'OK' if ok_ram else 'OVER'}")
    return 0 if (ok_rom and ok_ram) else 1


if __name__ == "__main__":
    sys.exit(main())
