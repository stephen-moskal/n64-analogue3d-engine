#!/bin/bash
# Print the disassembly of one function from an engine ELF, to check what the
# compiler made of a hot loop (docs/HARDWARE.md, "CPU caches and code placement").
#
#   libdragon exec bash tools/disasm.sh build/debug/engine-debug.elf mat4_mul_vec3
#
# Counting instructions: pipe through `grep -c lwc1` (FP loads), `swc1` (FP stores),
# `mul.s`, and so on.
set -euo pipefail
if [ $# -ne 2 ]; then
    echo "usage: $0 <elf> <function>" >&2
    exit 2
fi
OBJDUMP="${N64_INST:-/n64_toolchain}/bin/mips64-elf-objdump"
out=$("$OBJDUMP" -d --no-show-raw-insn "$1" | awk -v f="<$2>:" '$2 == f {p=1} p && /^$/ {p=0} p')   # read to the end: an early exit would SIGPIPE objdump under pipefail
if [ -z "$out" ]; then
    echo "function $2 not found in $1 (inlined, or a different name?)" >&2
    exit 1
fi
echo "$out"
