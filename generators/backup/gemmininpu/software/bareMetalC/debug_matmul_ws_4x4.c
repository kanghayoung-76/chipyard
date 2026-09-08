// Debug test: DIM×DIM WS matmul on DIM=16 hardware
// A = sequential values in full DIM×DIM, B = DIM×DIM identity
// Expected: C = A*B = A  (trivially verifiable)
// Run on BOTH Verilator (VelaGemminiRocketConfig) and FPGA (VelaRocketHugeGemmini)
// Compare [SW-*] lines in outputs to verify clocked-buffer correctness.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini_testutils.h"

#define A_ADDR   0
#define B_ADDR   DIM
#define D_ADDR   (2*DIM)

int main() {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall failed");
        exit(1);
    }
#endif

    static elem_t  A[DIM][DIM] row_align(1);
    static elem_t  B[DIM][DIM] row_align(1);
    static elem_t  D[DIM][DIM] row_align(1);
    static elem_t  C[DIM][DIM] row_align(1);
    static elem_t  gold[DIM][DIM];

    // A: sequential values (cast to elem_t; wraps for values > 127)
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            A[i][j] = (elem_t)(i * DIM + j);

    // B: full DIM×DIM identity -> C = A*B = A
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            B[i][j] = (i == j) ? 1 : 0;

    // D: all zeros (no bias)
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            D[i][j] = 0;

    printf("[SW-INPUT-A]\n");
    printMatrix(A);
    printf("[SW-INPUT-B]\n");
    printMatrix(B);

    // Accumulator destination (bit ADDR_LEN-1 = overwrite mode)
    uint32_t C_acc = (1u << (ADDR_LEN - 1));

    // --- Hardware setup ---
    gemmini_flush(0);
    gemmini_config_ld(DIM * sizeof(elem_t));
    gemmini_config_ex(WEIGHT_STATIONARY, NO_ACTIVATION, 0);
    gemmini_extended_config_st(DIM * sizeof(elem_t), NO_ACTIVATION, ACC_SCALE_IDENTITY);

    // Load full DIM×DIM tiles into scratchpad
    gemmini_mvin(A, A_ADDR);
    gemmini_mvin(B, B_ADDR);
    gemmini_mvin(D, D_ADDR);
    gemmini_fence();

    // WS: preload B (weights) then compute C_acc = D + A*B
    gemmini_preload(B_ADDR, C_acc);
    gemmini_compute_preloaded(A_ADDR, D_ADDR);
    gemmini_fence();

    // Read back full DIM×DIM result from accumulator
    gemmini_mvout(C, C_acc);
    gemmini_fence();

    // --- CPU golden reference: gold = D + A*B = A (B is identity) ---
    matmul(A, B, D, gold);

    printf("[SW-GOLDEN]\n");
    printMatrix(gold);
    printf("[SW-RESULT]\n");
    printMatrix(C);

    // Compare full DIM×DIM
    int pass = 1;
    for (int i = 0; i < DIM; i++) {
        for (int j = 0; j < DIM; j++) {
            if (C[i][j] != gold[i][j]) {
                printf("[SW-DIFF] C[%d][%d] got=%d expected=%d\n",
                       i, j, (int)C[i][j], (int)gold[i][j]);
                pass = 0;
            }
        }
    }

    if (pass)
        printf("[SW-PASS] debug_matmul_ws_4x4 PASSED\n");
    else
        printf("[SW-FAIL] debug_matmul_ws_4x4 FAILED\n");

    exit(pass ? 0 : 1);
}
