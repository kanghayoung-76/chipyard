# Makefile for the FMXCVU19P SGMII PCS diagnostic (Linux userspace).
#
# Statically linked so it runs on a minimal rootfs with no ethtool/devmem.
# Mirrors the `linux` target of ../memtest/Makefile.

ROOT_DIR  := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR := $(ROOT_DIR)/build

LINUX_CC     ?= $(RISCV)/bin/riscv64-unknown-linux-gnu-gcc
LINUX_CFLAGS  = -O2 -std=gnu11 -Wall -static -D_FILE_OFFSET_BITS=64

EXE := $(BUILD_DIR)/mdioread

.PHONY: default clean
default: $(EXE)

$(EXE): mdioread.c | $(BUILD_DIR)
	$(LINUX_CC) $(LINUX_CFLAGS) -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
