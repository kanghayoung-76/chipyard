// See LICENSE for license details.
//
// diagonal_test.c — full DIM×DIM diagonal matrix test for Gemmini NPU (WEIGHT-STATIONARY)
//
// Strategy: full DIM×DIM diagonal matrix occupying the entire hardware tile.
//
//   A[i][i] = i+1  for i = 0..DIM-1  (values 1..DIM on diagonal)  ← activation matrix
//   A[i][j] = 0    for i != j
//   B = DIM x DIM identity matrix                                   ← weight matrix (stationary)
//   Expected C = A × B = A  (diagonal values 1..DIM)
//
// ============================================================================
// WEIGHT-STATIONARY (WS) vs OUTPUT-STATIONARY (OS) — instruction-level diff
// ============================================================================
//
//  OS flow (reference):                   WS flow (this file):
//  ─────────────────────────────────────  ────────────────────────────────────────
//  config_ex(OUTPUT_STATIONARY, ...)      config_ex(WEIGHT_STATIONARY, ...)
//  preload(B_sp_addr, C_sp_addr)          preload(B_sp_addr, C_acc_addr)
//    rs1=B_sp_addr: load B into PEs         rs1=B_sp_addr: B loaded into PE weight
//    rs2=C_sp_addr: scratchpad output        registers from scratchpad
//                                           rs2=C_acc_addr: accumulator output
//  compute_preloaded(A_sp_addr,           compute_preloaded(A_sp_addr, D_sp_addr)
//                    GARBAGE_ADDR)          rs1=A_sp_addr: activation (diagonal)
//    rs1=A: activation (diagonal)           rs2=D_sp_addr: zero bias (no partial sum)
//    rs2=GARBAGE: D=0 (no bias)
//  mvout(C_gemmini, C_sp_addr)            mvout(C_gemmini, C_acc_addr)
//    source: scratchpad at 2*DIM            source: accumulator at 0x80000000
//
// Key address layout (WS):
//   A_sp_addr: 0           (activation rows)
//   B_sp_addr: DIM         (weight rows — loaded into PEs via preload)
//   D_sp_addr: 2*DIM       (zero bias rows)
//   C_acc_addr: 0x80000000 (accumulator overwrite, bits[31:30]=10)
//
// ============================================================================
// ILA Probe Points — Vivado hierarchy and expected waveform
// ============================================================================
//
// RTL hierarchy (verify exact instance names in Vivado netlist browser after
// synthesis; Chisel-generated names match the val identifiers in Scala source):
//
//   ChipTop/tile_prci_domain/tile/core/
//     executeController/                      ← ExecuteController.scala
//       io_cmd_bits_cmd_inst_funct            — RoCC opcode (4b):
//                                               0=config, 2=mvin, 3=mvout,
//                                               4=compute_preloaded/stay, 6=preload
//       io_cmd_bits_cmd_rs1                   — instruction rs1
//       io_cmd_bits_cmd_rs2                   — instruction rs2
//       wontolic/                             ← WontolicWithDelays (val wontolic = ...)
//         io_a_valid                          — A row data decoupled valid
//                                               (NOT a RoCC instruction; io.a carries
//                                               Vec(ma_length=16, int8) activation rows)
//         io_a_bits_0 .. io_a_bits_15         — elements of the current A row
//         io_req_valid                        — new tile request from ExecuteController
//         io_resp_valid                       — one output row is ready  ← ILA TRIGGER
//         io_resp_bits_last                   — this is the last output row of the tile
//         io_resp_bits_is_mpgemm              — is_mpgemm mode flag (2-cycle delayed)
//         mpexeunit/                          ← MpExeUnit
//           io_in_valid_0                     — first element valid (all 16 share same value)
//           io_in_is_mpgemm                   — 0=int8 path, 1=mixed-precision
//           io_out_valid_0                    — output valid  (2 cycles after io_in_valid_0)
//           io_out_is_mpgemm                  — ShiftRegister(io_in_is_mpgemm, 2)
//           io_out_c_0 .. io_out_c_15         — int8 output columns (aligned with out_valid)
//           io_out_c_16 .. io_out_c_63        — mp output columns (zero when is_mpgemm=0)
//
// ============================================================================
// Pipeline latency (from mpexeunit.io_in_valid to mpexeunit.io_out_valid):
//
//   Stage 1: Buffvector — RegNext(valid) and double-buffer data     +1 cycle
//            buffvectorarray_I / io_out_valid = RegNext(io_in_valid_I)
//            buffvectorarray_I / io_out_a     = data latched 1 cycle ago
//
//   Stage 2: PE — ShiftRegister(mul, 1) delays DATA by 1 cycle extra,
//            but valid passes through PE unregistered (= Buffvector.out_valid).
//            → At PE / Mularray output: out_result / out_sum is 1 cycle BEHIND out_valid
//
//            inside mularraybundle_int8_J (Mularray for output column J):
//              pe_array_I / io_out_valid  = Buffvector[I].out_valid        +1 cycle from input
//              pe_array_I / io_out_result = ShiftRegister(mul_unit.out, 1) +2 cycles from input
//              *** io_out_result LAGS io_out_valid by 1 cycle ***
//
//            mularraybundle_int8_J / io_out_valid = OR(pe_array[*].out_valid) +1 cycle
//            mularraybundle_int8_J / io_out_sum   = AdderTree(pe.out_result)  +2 cycles
//              *** io_out_sum LAGS io_out_valid by 1 cycle ***
//
//   Stage 3: Buffadderlight — RegNext(in_valid) compensates the lag:
//            buffadderarray_J / io_in_valid   = Mularray.out_valid     +1 cycle
//            buffadderarray_J / io_in_result  = Mularray.out_sum       +2 cycles
//            in_valid_next = RegNext(in_valid)                         +2 cycles ← now aligned
//            buffadderarray_J / io_out_valid  = in_valid_next          +2 cycles ✓
//            buffadderarray_J / io_out_c      = base_d + io_in_result,
//                                               gated by in_valid_next +2 cycles ✓
//              *** io_out_c and io_out_valid are SYNCHRONIZED ***
//
//   TOTAL: 2 cycles from mpexeunit.io_in_valid to mpexeunit.io_out_valid / io_out_c
//
//   io_out_is_mpgemm = ShiftRegister(io_in_is_mpgemm, 2)  ← confirms 2-cycle depth
//
// ============================================================================
// WS-specific data flow in hardware (differs from OS):
//
//   In WEIGHT_STATIONARY mode:
//     • compute_preloaded(A_sp_addr, B_sp_addr):
//         B columns are loaded into PE weight registers from the scratchpad.
//         A rows (activation) then stream through the systolic array.
//         PE[i][j] computes:  sum_k( A[i][k] * B_weight[k][j] )
//         B weight registers remain FIXED across the full 16-row A pass.
//     • preload(GARBAGE_ADDR, C_acc_addr):
//         GARBAGE_ADDR in rs1 → hardware sets PE weight registers to zero
//         (no data is preloaded from scratchpad via this instruction).
//         C_acc_addr (0xC0000000) → output is written to ACCUMULATOR RAM,
//         not scratchpad.  mvout then reads back from the accumulator.
//
//   ILA observation difference (WS vs OS):
//     PROBE 5 (preload):
//       OS: rs1 = B_sp_addr (DIM=16)      ← B data flows from scratchpad to PEs here
//       WS: rs1 = B_sp_addr (DIM)         ← same: B loaded into PE weight registers
//           rs2 = C_acc_addr (0x80000000) ← accumulator output (overwrite mode)
//     PROBE 6 (compute_preloaded):
//       OS: rs2 = GARBAGE_ADDR            ← no D/bias
//       WS: rs2 = D_sp_addr (2*DIM)       ← zero bias; A rows stream through PEs
//                                            whose weights are already loaded from PROBE 5
//
// ============================================================================
// Expected ILA waveform for full DIM×DIM diagonal test (relative to T=0 = first
// cycle where mpexeunit.io_in_valid_0 = 1, i.e. first A row entering MpExeUnit):
//
//   Hardware processes DIM=16 A rows (T=0..15), outputs valid 2 cycles later (T=2..17).
//   B weight registers are pre-loaded before T=0 during preload.
//
//   T  io_in_valid  io_out_valid  out_c_0  out_c_1  ...  out_c_15  out_last
//  ---+------------+--------------+---------+---------+----+---------+--------
//   0 |     1      |      0       |    -    |    -    |    |    -    |   0
//   1 |     1      |      0       |    -    |    -    |    |    -    |   0
//   2 |     1      |      1       |    1    |    0    |    |    0    |   0   ← row 0: C[0][0]=1
//   3 |     1      |      1       |    0    |    2    |    |    0    |   0   ← row 1: C[1][1]=2
//   4 |     1      |      1       |    0    |    0    |    |    0    |   0   ← row 2: C[2][2]=3
//  ...|    ...     |     ...      |    0    |    0    |    |    0    |   0
//  17 |     0      |      1       |    0    |    0    |    |   16    |   1   ← row 15: C[15][15]=16, last
//  18 |     0      |      0       |    -    |    -    |    |    -    |   0
//
//   io_resp_valid = mpexeunit.io_out_valid.head  (same as io_out_valid above)
//   io_resp_bits_last  rises only at T=17 (2 cycles after last input row T=15)
//
// ============================================================================
// Sub-module waveform for column J=0 (mularraybundle_int8_0, buffadderarray_0):
//
//   T  Mularray_out_valid  Mularray_out_sum  buffadd_in_valid  buffadd_out_valid  buffadd_out_c
//  ---+-------------------+----------------+------------------+------------------+--------------
//   0 |        0          |       0        |        0         |        0         |      0
//   1 |        1          |       0        |        1         |        0         |      0
//   2 |        1          |       1 ◄ row0 |        1         |        1         |      1 ✓
//   3 |        1          |       0        |        1         |        1         |      0
//  ...|       ...         |       0        |       ...        |       ...        |      0
//
//   ◄ At T=2: Mularray.out_valid=1 reflects row 1 being valid internally,
//     BUT Mularray.out_sum=1 carries row 0's accumulated product (1-cycle lag).
//     Buffadderlight.in_valid_next=RegNext(1 from T=1)=1 at T=2 compensates this.
//
// ============================================================================
// PE-level skew (visible in ILA, compensated by Buffadderlight):
//   Inside mularraybundle_int8_0 / pe_array_0:
//   T  pe_out_valid  pe_out_result
//  ---+-------------+--------------
//   0 |      0      |      0
//   1 |      1      |      0   ← valid HIGH but data is still 0 (1-cycle skew)
//   2 |      1      |      1   ← data for row 0 arrives here (1 cycle after out_valid rose)
//   3 |      1      |      0
//
// ============================================================================
// ILA trigger recommendation:
//   Arm on: wontolic/io_resp_valid (rising edge)  — fires at T=2
//   Capture: ~20 cycles post-trigger
//   Key probes: io_out_valid_0, io_out_c_0, io_out_c_1, io_out_c_2, io_out_c_3,
//               io_resp_bits_last,
//               mularraybundle_int8_0/io_out_valid, mularraybundle_int8_0/io_out_sum,
//               mularraybundle_int8_0/pe_array_0/io_out_result,
//               buffadderarray_0/io_out_valid, buffadderarray_0/io_out_c
//   NOTE: buffadderarray_0/io_in_valid and io_in_result are INPUT ports —
//         they do NOT appear as independent nets in Vivado ILA.
//         Use mularraybundle_int8_0/io_out_valid and io_out_sum instead
//         (same electrical net, probed at the driving module's output).
// ============================================================================

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini_testutils.h"

#define SUB_DIM DIM  // full DIM×DIM diagonal — all rows and columns active

int main() {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall failed");
        exit(1);
    }
#endif

    // -------------------------------------------------------------------------
    // ILA idle check: wontolic/io_req_valid = 0, io_resp_valid = 0
    // -------------------------------------------------------------------------
    gemmini_flush(0);

    // Static arrays for DMA alignment (row_align satisfies Gemmini DMA requirements)
    static elem_t A[DIM][DIM]         row_align(1);
    static elem_t B[DIM][DIM]         row_align(1);
    static elem_t D[DIM][DIM]         row_align(1);  // zero bias (static = zero-init)
    static elem_t C_gemmini[DIM][DIM] row_align(1);

    elem_t C_cpu[DIM][DIM];

    printf("=== Full %dx%d Diagonal Matrix Test (WEIGHT-STATIONARY) ===\n", DIM, DIM);
    printf("DIM=%d  elem_t=%d byte\n", DIM, (int)sizeof(elem_t));

    // -------------------------------------------------------------------------
    // Build A: DIM×DIM diagonal matrix
    //   A[i][i] = i+1  for i = 0..DIM-1  (values 1..DIM)
    //   A[i][j] = 0    for i != j
    //
    // For output column J: PE[J] in Mularray_int8[J] computes (J+1) * 1 = J+1.
    // -------------------------------------------------------------------------
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            A[i][j] = (i == j) ? (elem_t)(i + 1) : (elem_t)0;

    // Build B: full DIM x DIM identity (all rows, not just SUB_DIM)
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            B[i][j] = (i == j) ? (elem_t)1 : (elem_t)0;

    // -------------------------------------------------------------------------
    // CPU reference: C_cpu = A x B  (= A since B is identity)
    // -------------------------------------------------------------------------
    for (int r = 0; r < DIM; r++) {
        for (int c = 0; c < DIM; c++) {
            int32_t acc = 0;
            for (int k = 0; k < DIM; k++)
                acc += (int32_t)A[r][k] * (int32_t)B[k][c];
            acc = acc >  127 ?  127 : acc;
            acc = acc < -128 ? -128 : acc;
            C_cpu[r][c] = (elem_t)acc;
        }
    }

    printf("\nA (%dx%d diagonal):\n", DIM, DIM);
    printMatrix(A);
    printf("\nB (%dx%d identity):\n", DIM, DIM);
    printMatrix(B);
    printf("\nCPU reference C = A x B:\n");
    printMatrix(C_cpu);

    // -------------------------------------------------------------------------
    // Scratchpad / accumulator layout  (row-addressed)
    //
    //   A rows:  spad [0     .. DIM-1]    activation matrix
    //   B rows:  spad [DIM   .. 2*DIM-1]  weight matrix (stationary)
    //   D rows:  spad [2*DIM .. 3*DIM-1]  zero bias
    //   C rows:  acc  [0x80000000 ..]     accumulator (overwrite mode, bit31=1 bit30=0)
    //
    // Address encoding:
    //   Bits [31:30] = 00  → scratchpad
    //   Bits [31:30] = 10  → accumulator overwrite (1 << (ADDR_LEN-1))
    //   Bits [31:30] = 11  → accumulator accumulate
    // -------------------------------------------------------------------------
    const size_t A_sp_addr  = 0;
    const size_t B_sp_addr  = DIM;
    const size_t D_sp_addr  = 2 * DIM;
    const size_t C_acc_addr = (size_t)(1u << (ADDR_LEN - 1));  // 0x80000000

    // -------------------------------------------------------------------------
    // ILA PROBE 1 — config_ld / config_st
    // executeController: io_cmd_bits_cmd_inst_funct = 0 (CONFIG_CMD)
    //   rs1[1:0] = 1 for LD, 2 for ST
    // -------------------------------------------------------------------------
    gemmini_config_ld(DIM * sizeof(elem_t));
    gemmini_config_st(DIM * sizeof(elem_t));

    // -------------------------------------------------------------------------
    // ILA PROBE 2 — mvin B (full identity) to spad[DIM]
    // executeController: io_cmd_bits_cmd_inst_funct = 2 (MVIN)
    //   Destination: B_sp_addr = DIM
    //   WS: B is loaded into scratchpad first; preload instruction later
    //   streams it into PE weight registers before activations flow through.
    // -------------------------------------------------------------------------
    gemmini_mvin(B, B_sp_addr);

    // -------------------------------------------------------------------------
    // ILA PROBE 3 — mvin A (DIM rows, diagonal values 1..DIM, zeros off-diagonal)
    // executeController: io_cmd_bits_cmd_inst_funct = 2 (MVIN)
    //   Destination: A_sp_addr = 0
    // -------------------------------------------------------------------------
    gemmini_mvin(A, A_sp_addr);

    // mvin D (all zeros — no bias)
    gemmini_mvin(D, D_sp_addr);

    // -------------------------------------------------------------------------
    // ILA PROBE 4 — config_ex  WEIGHT_STATIONARY, no activation, shift=0
    // executeController: io_cmd_bits_cmd_inst_funct = 0, rs1[3:2]=1 (WS)
    //   (OS used rs1[3:2]=0; WS sets bit 2 to select WEIGHT_STATIONARY)
    // -------------------------------------------------------------------------
    gemmini_config_ex(WEIGHT_STATIONARY, NO_ACTIVATION, 0);

    // -------------------------------------------------------------------------
    // ILA PROBE 5 — preload (WS: B weights in rs1, accumulator address in rs2)
    // executeController: io_cmd_bits_cmd_inst_funct = 6 (PRELOAD)
    //   rs1 = B_sp_addr (DIM)           → hardware reads B rows from scratchpad
    //                                      and loads them into PE weight registers.
    //   rs2 = C_acc_addr (0x80000000)   → output written to ACCUMULATOR RAM
    //                                      bits[31:30]=10 → overwrite mode
    // -------------------------------------------------------------------------
    gemmini_preload(B_sp_addr, C_acc_addr);

    // -------------------------------------------------------------------------
    // ILA PROBE 6 — compute_preloaded  *** MAIN PROBE ***
    // executeController: io_cmd_bits_cmd_inst_funct = 4 (COMPUTE_AND_FLIP)
    //   rs1 = A_sp_addr  → activation rows (diagonal matrix, DIM rows from spad[0])
    //                       Hardware streams A rows through the PE array.
    //                       PE weights (B) were already loaded by PROBE 5 preload.
    //   rs2 = D_sp_addr  → zero bias (no partial sum input)
    //
    // Hardware computes: C = A × B = diagonal × identity = diagonal
    //
    // What to watch in ILA:
    //   wontolic/io_resp_valid       — use as ILA trigger (rising edge)
    //   mpexeunit/io_in_valid_0      — A rows being fed (16 consecutive pulses)
    //   mpexeunit/io_in_is_mpgemm    — must be 0 (int8 path, not mp)
    //   mpexeunit/io_out_valid_0     — output valid, appears 2 cycles after in_valid
    //   mpexeunit/io_out_c_0..3      — output values (non-zero only on 4 specific cycles)
    //   wontolic/io_resp_bits_last   — rises on last output row (T=17 relative to T=0)
    //
    // Expected non-zero output events (T=0 = first io_in_valid_0=1 cycle):
    //   T=2+i: io_out_c_i = i+1  for i=0..DIM-1
    //     e.g. T=2: io_out_c_0=1, T=3: io_out_c_1=2, ... T=17: io_out_c_15=16
    //   Each row i produces exactly one non-zero column at position i.
    //
    // Sub-module observations (column J=0 as example):
    //
    //   [Mularray out, 1-cycle lag visible]:
    //   T=1: mularraybundle_int8_0/io_out_valid=1, io_out_sum=0  (lag: data not ready yet)
    //   T=2: mularraybundle_int8_0/io_out_valid=1, io_out_sum=1  (row 0 result, 1 cycle late)
    //   T=3+: io_out_sum=0
    //
    //   [PE skew visible]:
    //   T=1: pe_array_0/io_out_valid=1, io_out_result=0  (valid rose but data=0)
    //   T=2: pe_array_0/io_out_valid=1, io_out_result=1  (data for row 0 arrives 1 cycle late)
    //
    //   [Buffadderlight compensation — output SYNCHRONIZED]:
    //   T=2: buffadderarray_0/io_out_valid=1, io_out_c=1  (aligned: both valid and data at T=2)
    //   T=3+: buffadderarray_0/io_out_valid=1, io_out_c=0
    //
    // Lane-to-hardware mapping for mismatch diagnosis:
    //   Output column J=0..15  → mularraybundle_int8_J, buffadderarray_J  (int8 path)
    //   Output column J=16..63 → mularraybundle_{J-16}, buffadderarray_J  (mp path, zero here)
    // -------------------------------------------------------------------------
    gemmini_compute_preloaded(A_sp_addr, D_sp_addr);

    // -------------------------------------------------------------------------
    // ILA PROBE 7 — mvout  (DMA reads C from ACCUMULATOR back to DRAM)
    // executeController: io_cmd_bits_cmd_inst_funct = 3 (MVOUT)
    //   source: C_acc_addr (0x80000000) — accumulator RAM, not scratchpad
    //   The accumulator holds int32 values; mvout applies ACC_SCALE_IDENTITY
    //   (1.0) and clips to int8 before writing to DRAM.
    // io_resp_valid has already pulsed 16 times during compute above.
    // -------------------------------------------------------------------------
    gemmini_mvout(C_gemmini, C_acc_addr);

    gemmini_fence();

    // -------------------------------------------------------------------------
    // Software result comparison
    // -------------------------------------------------------------------------
    printf("\nGemmini C_gemmini:\n");
    printMatrix(C_gemmini);

    int pass = 1;
    int mismatch_count = 0;

    printf("\n--- Element-wise Comparison (CPU vs Gemmini) ---\n");
    for (int r = 0; r < DIM; r++) {
        for (int c = 0; c < DIM; c++) {
            if (C_gemmini[r][c] != C_cpu[r][c]) {
                printf("  MISMATCH [row=%2d][col=%2d]: expected=%4d  got=%4d\n",
                       r, c, (int)C_cpu[r][c], (int)C_gemmini[r][c]);
                // Map output column c to RTL lane:
                //   int8 lanes  c=0..15: mularraybundle_int8_%d and buffadderarray_%d
                //   mp   lanes  c=16..63: mularraybundle_%d and buffadderarray_%d
                if (c < DIM)   // i8Count = mp_ma_num/4 = (DIM*4)/4 = DIM = 16
                    printf("    ILA lane: int8 -> mularraybundle_int8_%d  buffadderarray_%d\n",
                           c, c);
                else
                    printf("    ILA lane: mp   -> mularraybundle_%d  buffadderarray_%d\n",
                           c - DIM, c);
                mismatch_count++;
                pass = 0;
            }
        }
    }

    if (pass) {
        printf("\nPASS: all %d elements match.\n", DIM * DIM);
        printf("Diagonal values (expected 1..%d):\n  ", DIM);
        for (int i = 0; i < DIM; i++)
            printf("[%d][%d]=%d ", i, i, (int)C_gemmini[i][i]);
        printf("\n");
        exit(0);
    } else {
        printf("\nFAIL: %d / %d elements mismatched.\n", mismatch_count, DIM * DIM);
        exit(1);
    }
}
