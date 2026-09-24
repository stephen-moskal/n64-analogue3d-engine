#!/usr/bin/env bash
# CI build steps (ROADMAP_v2 P1.9). Runs inside the libdragon container, with
# libdragon already installed into $N64_INST:
#   - GitHub Actions: .github/workflows/build.yml installs libdragon from the
#     submodule, then runs this script.
#   - Locally:        libdragon exec bash tools/ci_build.sh
set -euo pipefail
cd "$(dirname "$0")/.."

JOBS="$(nproc 2>/dev/null || echo 4)"

# Builds with the output shown, and fails on any compiler warning: libdragon's
# n64.mk keeps the -Wunused-* family out of -Werror, so without this an unused
# variable only prints (S8: one that exists only for an assert, in release)
build_rom() {
    local log
    log="$(mktemp)"
    make -j"$JOBS" "$@" 2>&1 | tee "$log"
    if grep -q "warning:" "$log"; then
        echo "::error::compiler warnings in the $* build (listed above)"
        exit 1
    fi
}

echo "::group::Debug ROM"
build_rom BUILD=debug
echo "::endgroup::"

echo "::group::Release ROM"
build_rom BUILD=release
echo "::endgroup::"

echo "::group::Host unit tests"
make -C tests/host clean >/dev/null
make -C tests/host run
echo "::endgroup::"

echo "::group::Budgets"
python3 tools/rom_budget.py --rom engine.z64 --elf build/release/engine.elf \
    --max-rom-kb 4096 --max-ram-kb 1024
echo "::endgroup::"

echo "::group::Hot text (I-cache layout)"
python3 tools/hot_text.py build/debug/engine-debug.elf
python3 tools/hot_text.py build/release/engine.elf
echo "::endgroup::"

echo "::group::Hot data (D-cache layout)"
python3 tools/hot_data.py build/debug/engine-debug.elf
python3 tools/hot_data.py build/release/engine.elf
echo "::endgroup::"

echo "CI build OK"
