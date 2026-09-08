#!/usr/bin/env bash
#
# build-linux.sh — build gemmini bareMetalC tests as STATIC Linux userspace
# binaries, runnable under Ubuntu on the FPGA.
#
# Why static:
#   gemmini's Makefile `%-linux` rule links DYNAMICALLY against the host
#   toolchain's glibc (>= 2.34). The board's Ubuntu 20.04 rootfs ships glibc
#   2.31, so a dynamic binary dies at load time with:
#       libc.so.6: version `GLIBC_2.34' not found
#   Linking `-static` bundles libc into the binary, so it runs regardless of
#   the target's glibc version.
#
# Usage:
#   ./build-linux.sh                 # build the default test list (see TESTS)
#   ./build-linux.sh diagonal_test   # build specific test(s) (name or name.c)
#
# NOTE: these run only where the Gemmini accelerator is present in the
# bitstream — tests issuing Gemmini RoCC ops will SIGILL on a core without it.
set -uo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$SRC_DIR/bareMetalC"
BUILD_DIR="$SRC_DIR/build-linux"

# --- locate the Linux RISC-V toolchain -------------------------------------
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
    echo "ERROR: $CC_NAME not found. Source the Chipyard env or set RISCV, then re-run." >&2
    exit 1
fi
TC_PREFIX="${CC%$CC_NAME}"
OBJDUMP="${TC_PREFIX}riscv64-unknown-linux-gnu-objdump"

# --- flags (mirror bareMetalC/Makefile CFLAGS, plus -static) ---------------
BENCH_COMMON="$SRC_DIR/riscv-tests/benchmarks/common"
CFLAGS=(
    -O2 -std=gnu99 -mcmodel=medany
    -march=rv64gc -mabi=lp64d
    -DPREALLOCATE=1 -DMULTITHREAD=1
    -ffast-math -fno-common -fno-builtin-printf
    -fno-tree-loop-distribute-patterns
    -DID_STRING= -DPRINT_TILE=0 -DFAST
    -static
    -I"$SRC_DIR/riscv-tests"
    -I"$SRC_DIR/riscv-tests/env"
    -I"$SRC_DIR"
    -I"$BENCH_COMMON"
)
LIBS=( -lm -lgcc )

# --- default test list (the standalone mains from bareMetalC/Makefile) -----
TESTS=(
    mvin_mvout mvin_mvout_zeros mvin_mvout_stride mvin_mvout_block_stride
    mvin_mvout_acc mvin_mvout_acc_zero_stride mvin_mvout_acc_stride
    mvin_mvout_acc_full mvin_mvout_acc_full_stride
    matmul_os matmul_ws matmul_ws_diff_bank matmul_ws_same_bank matmul matmul_perf
    raw_hazard aligned padded mvin_scale
    conv conv_stride conv_rect conv_rect_pool conv_with_pool conv_with_rot180
    conv_with_kernel_dilation conv_with_input_dilation
    conv_with_input_dilation_and_rot180 conv_with_input_dilation_and_neg_padding
    conv_trans_output_1203 conv_trans_weight_1203 conv_trans_weight_0132
    conv_trans_input_3120 conv_trans_input_3120_with_kernel_dilation
    conv_first_layer conv_dw conv_perf conv_dw_perf
    tiled_matmul_os tiled_matmul_ws tiled_matmul_ws_At tiled_matmul_ws_Bt
    tiled_matmul_ws_full_C tiled_matmul_ws_low_D tiled_matmul_ws_igelu
    tiled_matmul_ws_layernorm tiled_matmul_ws_softmax tiled_matmul_ws_perf
    tiled_matmul_cpu tiled_matmul_option
    transpose matrix_add resadd resadd_stride global_average gemmini_counter
    template perf perf_total gemm ternary_gemm gemv_single gemv_double
    diagonal_test debug_matmul_ws_4x4 cpu_diagonal_test cpu_debug_matmul
)

if [ "$#" -gt 0 ]; then
    TESTS=()
    for a in "$@"; do TESTS+=( "$(basename "${a%.c}")" ); done
fi

mkdir -p "$BUILD_DIR"
echo "CC     = $CC"
echo "BUILD  = $BUILD_DIR"
echo "FLAGS  = rv64gc / lp64d, -static (Linux userspace)"
echo

pass=0; fail=0; failed=()
for t in "${TESTS[@]}"; do
    src="$TEST_DIR/$t.c"
    if [ ! -f "$src" ]; then
        echo "SKIP  $t (no $t.c)"; continue
    fi
    if "$CC" "${CFLAGS[@]}" "$src" "${LIBS[@]}" -o "$BUILD_DIR/$t.riscv" 2>"$BUILD_DIR/$t.err"; then
        echo "OK    $t"
        pass=$((pass+1))
    else
        echo "FAIL  $t  (see $BUILD_DIR/$t.err)"
        fail=$((fail+1)); failed+=( "$t" )
    fi
done

echo
echo "Built $pass, failed $fail. Binaries in $BUILD_DIR/ (statically linked)."
[ "$fail" -gt 0 ] && printf 'Failed: %s\n' "${failed[*]}"
echo "Copy the .riscv files to the FPGA's Ubuntu rootfs and run them there."
exit 0
