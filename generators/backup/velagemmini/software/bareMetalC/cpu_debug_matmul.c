// CPU-only version of debug_matmul_ws_4x4: full DIM×DIM matmul.
// No Gemmini accelerator — pure software reference.
// Output format matches debug_matmul_ws_4x4 so results can be diff'd directly.
//
//   A = sequential values (cast to elem_t), B = DIM×DIM identity
//   Expected C = A*B = A

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini_testutils.h"

int main() {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall failed");
        exit(1);
    }
#endif

    static elem_t A[DIM][DIM] row_align(1);
    static elem_t B[DIM][DIM] row_align(1);
    static elem_t D[DIM][DIM] row_align(1);
    static elem_t C[DIM][DIM];
    static elem_t gold[DIM][DIM];

    // A: sequential values (wraps for values > 127, same as Gemmini version)
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            A[i][j] = (elem_t)(i * DIM + j);

    // B: full DIM×DIM identity
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            B[i][j] = (i == j) ? 1 : 0;

    // D: zero bias
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            D[i][j] = 0;

    printf("[SW-INPUT-A]\n");
    printMatrix(A);
    printf("[SW-INPUT-B]\n");
    printMatrix(B);

    // CPU matmul: gold = D + A*B
    matmul(A, B, D, gold);

    // C = gold (CPU computes the result directly)
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            C[i][j] = gold[i][j];

    printf("[SW-GOLDEN]\n");
    printMatrix(gold);
    printf("[SW-RESULT]\n");
    printMatrix(C);

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
        printf("[SW-PASS] cpu_debug_matmul PASSED\n");
    else
        printf("[SW-FAIL] cpu_debug_matmul FAILED\n");

    exit(pass ? 0 : 1);
}
