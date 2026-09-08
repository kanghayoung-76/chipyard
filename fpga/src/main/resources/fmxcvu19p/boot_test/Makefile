# Makefile for boot_test - bare-metal diagnostic payload
# Replaces OpenSBI on SD card (sector 34) for bring-up debugging.
#
# Usage:
#   make              - Build all (elf, bin, dump)
#   make clean        - Remove build directory
#
# Prerequisites: RISCV env var must point to the RISC-V toolchain.
# After building, write to SD card with:
#   sudo dd if=build/boot_test.bin of=/dev/sdX bs=512 seek=34 conv=fsync

ROOT_DIR  := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR := $(ROOT_DIR)/build

CC       = $(RISCV)/bin/riscv64-unknown-elf-gcc
OBJCOPY  = $(RISCV)/bin/riscv64-unknown-elf-objcopy
OBJDUMP  = $(RISCV)/bin/riscv64-unknown-elf-objdump

# Match the ISA used in sdboot (rv64ima + CSR + fence.i extensions)
CFLAGS   = -march=rv64ima_zicsr_zifencei -mabi=lp64 -mcmodel=medany
CFLAGS  += -O1 -std=gnu11 -Wall -nostartfiles -fno-common -g
LFLAGS   = -static -nostdlib -T $(ROOT_DIR)/link.lds

.PHONY: default elf bin dump clean
default: elf bin dump

# --- ELF ---
elf := $(BUILD_DIR)/boot_test.elf
$(elf): $(ROOT_DIR)/start.S $(ROOT_DIR)/main.c $(ROOT_DIR)/link.lds
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ \
	    $(ROOT_DIR)/start.S $(ROOT_DIR)/main.c
elf: $(elf)

# --- Flat binary (what gets written to the SD card) ---
bin := $(BUILD_DIR)/boot_test.bin
$(bin): $(elf)
	$(OBJCOPY) -O binary $< $@
bin: $(bin)

# --- Disassembly for inspection ---
dump := $(BUILD_DIR)/boot_test.dump
$(dump): $(elf)
	$(OBJDUMP) -D -S $< > $@
dump: $(dump)

clean:
	rm -rf $(BUILD_DIR)
