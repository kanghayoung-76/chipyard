// M-mode trap entry. Saves every caller-saved register, because handle_trap is
// an ordinary C function and the ABI lets it clobber ra, t0-t6 and a0-a7. The
// earlier version saved only ra, a0-a2 and t0-t2, so a trap taken inside a loop
// corrupted that loop's address and counter registers -- which is why the C2
// and C3 tests stopped making progress after their first refused load.
//
// Callee-saved registers (s0-s11, sp, gp, tp) need no saving here: handle_trap
// preserves them itself.

  .section .text
  .globl trap_entry
  .align 4
trap_entry:
  addi  sp, sp, -128
  sd    ra,   0(sp)
  sd    t0,   8(sp)
  sd    t1,  16(sp)
  sd    t2,  24(sp)
  sd    t3,  32(sp)
  sd    t4,  40(sp)
  sd    t5,  48(sp)
  sd    t6,  56(sp)
  sd    a0,  64(sp)
  sd    a1,  72(sp)
  sd    a2,  80(sp)
  sd    a3,  88(sp)
  sd    a4,  96(sp)
  sd    a5, 104(sp)
  sd    a6, 112(sp)
  sd    a7, 120(sp)

  csrr  a0, mcause
  csrr  a1, mepc
  csrr  a2, mtval
  call  handle_trap          // returns the mepc to resume at; may also have
  csrw  mepc, a0             // rewritten mstatus.MPP to come back as M-mode

  ld    ra,   0(sp)
  ld    t0,   8(sp)
  ld    t1,  16(sp)
  ld    t2,  24(sp)
  ld    t3,  32(sp)
  ld    t4,  40(sp)
  ld    t5,  48(sp)
  ld    t6,  56(sp)
  ld    a0,  64(sp)
  ld    a1,  72(sp)
  ld    a2,  80(sp)
  ld    a3,  88(sp)
  ld    a4,  96(sp)
  ld    a5, 104(sp)
  ld    a6, 112(sp)
  ld    a7, 120(sp)
  addi  sp, sp, 128
  mret

// enter_smode(fn): run fn in S-mode and come back here. Unused by the C2/C3
// tests, kept so the one assembly file serves every test in this directory.
  .globl enter_smode
  .align 4
enter_smode:
  addi  sp, sp, -16
  sd    ra, 0(sp)

  la    t0, smode_return
  la    t1, m_resume
  sd    t0, 0(t1)

  csrr  t0, mstatus
  li    t1, 3
  slli  t1, t1, 11           // MPP mask
  not   t1, t1
  and   t0, t0, t1
  li    t1, 1
  slli  t1, t1, 11           // MPP = S
  or    t0, t0, t1
  csrw  mstatus, t0

  csrw  mepc, a0
  mret

smode_return:
  ld    ra, 0(sp)
  addi  sp, sp, 16
  ret
