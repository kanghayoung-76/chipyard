// M-mode trap entry and the S-mode trampoline for pmpobs.c.
// Observation support only; nothing here is part of the design under test.

  .section .text
  .globl trap_entry
  .align 4
trap_entry:
  addi  sp, sp, -64
  sd    ra,  0(sp)
  sd    a0,  8(sp)
  sd    a1, 16(sp)
  sd    a2, 24(sp)
  sd    t0, 32(sp)
  sd    t1, 40(sp)
  sd    t2, 48(sp)

  csrr  a0, mcause
  csrr  a1, mepc
  csrr  a2, mtval
  call  handle_trap          // returns the mepc to resume at; may also have
  csrw  mepc, a0             // rewritten mstatus.MPP to come back as M-mode

  ld    ra,  0(sp)
  ld    a0,  8(sp)
  ld    a1, 16(sp)
  ld    a2, 24(sp)
  ld    t0, 32(sp)
  ld    t1, 40(sp)
  ld    t2, 48(sp)
  addi  sp, sp, 64
  mret

// enter_smode(fn): run fn in S-mode and come back here.
//
// A trap from S-mode sets mstatus.MPP = S, so an mret after the S-mode body's
// ecall would land back in S-mode. handle_trap therefore forces MPP = M and
// resumes at m_resume, which is set here before dropping down.
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
