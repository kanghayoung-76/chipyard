// See LICENSE.Sifive for license details.
#include <platform.h>
#include <smp.h>
#include "common.h"

  .section .text.init
  .option norvc
  .globl _prog_start
_prog_start:
  smp_pause(s1, s2)
  // Stack MUST sit above the region copy() writes, i.e. above
  // PAYLOAD_DEST + PAYLOAD_SIZE_B (sd.c). copy() writes *p++ upward from
  // PAYLOAD_DEST for a fixed PAYLOAD_SIZE_B bytes, so a stack inside that
  // window is overwritten mid-transfer: main()'s saved ra is replaced with
  // payload bytes, "BOOT" still prints, and the `ret` from main() jumps to
  // garbage instead of reaching the `jr s1` below -- a completely silent
  // failure with no console output after BOOT.
  //
  // The original value here was PAYLOAD_DEST + 0xffff000 (256 MiB - 4 KiB),
  // derived from platform.h's MEMORY_MEM_SIZE of 0x10000000. That is stale:
  // this board has 36 GiB (NUMOFMEMORY="2"). It was safe only while
  // PAYLOAD_SIZE_B was 80 MiB; raising it to 320 MiB put the copy 64 MiB past
  // the stack.
  //
  // 0x18000000 = 384 MiB, clearing the current 320 MiB PAYLOAD_SIZE_B by
  // 64 MiB. If PAYLOAD_SIZE_B is ever raised again, raise this too.
  li sp, (PAYLOAD_DEST + 0x18000000)
  call main
  smp_resume(s1, s2)
  csrr a0, mhartid // hartid for next level bootloader
  la a1, dtb // dtb address for next level bootloader
  li s1, PAYLOAD_DEST
  jr s1

  .section .dtb
  .align 3
dtb:
