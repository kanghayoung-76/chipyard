// start.S - Entry point for bare-metal boot diagnostic payload
// Loaded by sdboot to 0x80000000.
// On entry: a0 = mhartid, a1 = DTB address (from sdboot)

  .section .text.init
  .option norvc
  .globl _start
_start:
  // Park all non-zero harts in WFI loop (single-core system, but be safe)
  csrr t0, mhartid
  bnez t0, _park

  // Set up stack (just below our .bss end + 64KB)
  la   sp, _stack_top

  // a0 = hartid, a1 = dtb address already set by sdboot head.S
  // Call C main
  call boot_main

  // If main returns, spin here
_park:
  wfi
  j _park
