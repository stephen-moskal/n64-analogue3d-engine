#!/usr/bin/env python3
"""Check ROM size and RAM footprint against budgets (ROADMAP_v2 P1.9).

Usage:
    python3 tools/rom_budget.py --rom engine.z64 --elf build/release/engine.elf \
        [--max-rom-kb 4096] [--max-ram-kb 1024]

RAM footprint = text + data + bss of the ELF (what is resident before any heap
allocation), read with the toolchain's `mips64-elf-size` (from $N64_INST/bin or
PATH). Prints a one-line summary; exits 1 when a budget is exceeded.
"""
import argparse
import os
import shutil
import subprocess
import sys


def elf_sizes(elf):
    tool = os.path.join(os.environ.get("N64_INST", ""), "bin", "mips64-elf-size")
    if not os.path.exists(tool):
        tool = shutil.which("mips64-elf-size") or ""
    if not tool:
        raise RuntimeError("mips64-elf-size not found (set N64_INST or run inside the libdragon container)")
    out = subprocess.run([tool, elf], check=True, capture_output=True, text=True).stdout
    # Berkeley format: "text data bss dec hex filename"
    fields = out.strip().splitlines()[-1].split()
    return int(fields[0]), int(fields[1]), int(fields[2])


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
    except (OSError, RuntimeError, subprocess.CalledProcessError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    ram = text + data + bss
    ok_rom = rom <= args.max_rom_kb * 1024
    ok_ram = ram <= args.max_ram_kb * 1024
    print(f"ROM {rom / 1024:.1f} KB (budget {args.max_rom_kb} KB) {'OK' if ok_rom else 'OVER'} | "
          f"RAM text {text} + data {data} + bss {bss} = {ram / 1024:.1f} KB "
          f"(budget {args.max_ram_kb} KB) {'OK' if ok_ram else 'OVER'}")
    return 0 if (ok_rom and ok_ram) else 1


if __name__ == "__main__":
    sys.exit(main())
