/* Linker script for boot_test payload.
 * Loaded by sdboot to PAYLOAD_DEST = 0x80000000.
 * Must fit in a few MB (sdboot loads 50MB, but keep it small). */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
  ram (rwx) : ORIGIN = 0x80000000, LENGTH = 0x10000000  /* 256MB DRAM region */
}

SECTIONS
{
  . = 0x80000000;

  .text : {
    *(.text.init)       /* start.S entry must be first */
    *(.text .text.*)
  } >ram

  .rodata : ALIGN(8) {
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
  } >ram

  .data : ALIGN(8) {
    *(.data .data.*)
    *(.sdata .sdata.*)
  } >ram

  .bss : ALIGN(8) {
    _bss_start = .;
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
    . = ALIGN(8);
    _bss_end = .;
  } >ram

  /* 64 KB stack placed right after BSS */
  . = ALIGN(16);
  _stack_bottom = .;
  . += 0x10000;
  _stack_top = .;

  _end = .;
}
