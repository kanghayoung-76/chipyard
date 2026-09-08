#!/usr/bin/env bash
#
# build-linux.sh — build velasaturn/software tests as LINUX userspace binaries
# (to run under Ubuntu on the VelaSaturn FPGA), as opposed to the bare-metal
# HTIF binaries produced by CMakeLists.txt (which only run in Spike/Verilator).
#
# Why this exists:
#   CMakeLists.txt links riscv64-unknown-elf + htif_nano.specs + htif.ld, so the
#   binary is a bare-metal M-mode image at 0x80000000. Its crt0 executes
#   `csrs mstatus,...`, which is illegal in U-mode -> Linux kills it with SIGILL.
#   This script instead links a normal Linux userspace ELF (glibc _start, no
#   privileged CSRs), while preserving the RVV vector code and the custom NPU
#   instruction (.word) in the sources.
#
# Usage:
#   ./build-linux.sh                # build the default source list
#   ./build-linux.sh vfpid_4d.c     # build specific source(s)
#
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build-linux"

# --- locate the Linux RISC-V toolchain -------------------------------------
# Prefer $RISCV, then Chipyard's conda env, then whatever is on PATH.
CC_NAME="riscv64-unknown-linux-gnu-gcc"
CANDIDATES=(
    "${RISCV:-}/bin/$CC_NAME"
    "$SRC_DIR/../../../.conda-env/riscv-tools/bin/$CC_NAME"
    "$CC_NAME"
)
CC=""
for c in "${CANDIDATES[@]}"; do
    if [ -n "$c" ] && command -v "$c" >/dev/null 2>&1; then CC="$c"; break; fi
done
if [ -z "$CC" ]; then
    echo "ERROR: $CC_NAME not found. Source the Chipyard env (e.g. 'source \$CHIPYARD/env.sh')" >&2
    echo "       or set RISCV to your linux toolchain prefix, then re-run." >&2
    exit 1
fi
TC_PREFIX="${CC%$CC_NAME}"
OBJDUMP="${TC_PREFIX}riscv64-unknown-linux-gnu-objdump"

# --- flags -----------------------------------------------------------------
# rv64gcv  : G(imafd)+C(compressed)+V(vector) — matches the RVV code in sources
# lp64d    : hard-double ABI
# -static  : avoid glibc-version / ld.so mismatch against the target rootfs
CFLAGS=(
    -O2 -std=gnu99 -Wall -Wextra
    -march=rv64gcv -mabi=lp64d -mcmodel=medany
    -fno-common -fno-builtin-printf
    -static
)
LDFLAGS=( -Wl,-u,_printf_float )
LIBS=( -lm )

# --- source list -----------------------------------------------------------
if [ "$#" -gt 0 ]; then
    SOURCES=( "$@" )
else
    SOURCES=(
        vfpid_4d.c
        vfpid_4f.c
        pid_scalar_float_optimized.c
        pid_scalar_double_optimized.c
        pid_vector_float_optimized.c
        pid_vector_double_optimized.c
    )
fi

mkdir -p "$BUILD_DIR"
echo "CC       = $CC"
echo "BUILD    = $BUILD_DIR"
echo "MARCH    = rv64gcv / lp64d (static, Linux userspace)"
echo

for src in "${SOURCES[@]}"; do
    name="$(basename "${src%.c}")"
    out="$BUILD_DIR/$name.riscv"
    echo ">> $src -> $out"
    "$CC" "${CFLAGS[@]}" "$SRC_DIR/$src" "${LDFLAGS[@]}" "${LIBS[@]}" -o "$out"
    "$OBJDUMP" -D "$out" > "$BUILD_DIR/$name.dump"
done

echo
echo "Done. Copy binaries in $BUILD_DIR/ to the FPGA's Ubuntu rootfs and run them there."
