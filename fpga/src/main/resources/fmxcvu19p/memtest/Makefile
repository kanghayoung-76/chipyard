# Makefile for FMXCVU19P DDR4 2-MIG Memory Test
#
# Reuses the sdboot build infrastructure (head.S, kprintf, platform.h, linker).
# RISCV environment variable must be set.

SDBOOT_DIR := $(abspath ../sdboot)
ROOT_DIR   := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR  := $(ROOT_DIR)/build

CC      = $(RISCV)/bin/riscv64-unknown-elf-gcc
OBJCOPY = $(RISCV)/bin/riscv64-unknown-elf-objcopy
OBJDUMP = $(RISCV)/bin/riscv64-unknown-elf-objdump

# Same flags as sdboot; no stdlib, bare-metal
CFLAGS  = -march=rv64ima_zicsr_zifencei -mcmodel=medany -O2 -std=gnu11
CFLAGS += -Wall -nostartfiles -fno-common -g
CFLAGS += -mabi=lp64 -DENTROPY=0 -DNONSMP_HART=0
CFLAGS += -I $(SDBOOT_DIR)/include -I $(SDBOOT_DIR)
CFLAGS += -DTL_CLK="100000000UL"   # 100 MHz peripheral bus clock (adjust if needed)

LFLAGS  = -static -nostdlib
LFLAGS += -L $(SDBOOT_DIR)/linker -T $(SDBOOT_DIR)/linker/sdboot.elf.lds

SRCS = $(SDBOOT_DIR)/head.S \
       memtest.c             \
       $(SDBOOT_DIR)/kprintf.c

# Linux cross-compiler (override with LINUX_CC=gcc for native build on FPGA)
LINUX_CC     ?= $(RISCV)/bin/riscv64-unknown-linux-gnu-gcc
LINUX_CFLAGS  = -O2 -std=gnu11 -Wall -static -D_FILE_OFFSET_BITS=64
LINUX_EXE    := $(BUILD_DIR)/memtest_linux

.PHONY: default elf bin dump linux clean

default: bin dump linux

elf := $(BUILD_DIR)/memtest.elf
$(elf): $(SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ $^

.PHONY: elf
elf: $(elf)

bin := $(BUILD_DIR)/memtest.bin
$(bin): $(elf)
	$(OBJCOPY) -O binary --change-addresses=-0x10000 $< $@

.PHONY: bin
bin: $(bin)

dump := $(BUILD_DIR)/memtest.dump
$(dump): $(elf)
	$(OBJDUMP) -D $< > $@

.PHONY: dump
dump: $(dump)

$(LINUX_EXE): memtest_linux.c | $(BUILD_DIR)
	$(LINUX_CC) $(LINUX_CFLAGS) -o $@ $<

.PHONY: linux
linux: $(LINUX_EXE)

$(BUILD_DIR):
	mkdir -p $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
