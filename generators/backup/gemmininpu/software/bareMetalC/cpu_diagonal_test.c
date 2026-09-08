// CPU-only version of diagonal_test: full DIM×DIM diagonal matrix multiply.
// No Gemmini accelerator — pure software reference.
// Output format matches diagonal_test so results can be diff'd directly.
//
//   A[i][i] = i+1  for i = 0..DIM-1
//   B = DIM×DIM identity
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

    // A: diagonal values 1..DIM
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            A[i][j] = (i == j) ? (elem_t)(i + 1) : 0;

    // B: identity matrix
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

    // CPU matmul: C = D + A*B
    matmul_short(A, B, D, C);

    printf("[SW-GOLDEN]\n");
    printMatrix(A);  // expected = A (identity multiplication)
    printf("[SW-RESULT]\n");
    printMatrix(C);

    int pass = 1;
    for (int i = 0; i < DIM; i++) {
        for (int j = 0; j < DIM; j++) {
            if (C[i][j] != A[i][j]) {
                printf("[SW-DIFF] C[%d][%d] got=%d expected=%d\n",
                       i, j, (int)C[i][j], (int)A[i][j]);
                pass = 0;
            }
        }
    }

    if (pass)
        printf("[SW-PASS] cpu_diagonal_test PASSED\n");
    else
        printf("[SW-FAIL] cpu_diagonal_test FAILED\n");

    exit(pass ? 0 : 1);
}
