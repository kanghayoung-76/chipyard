# ethdiag — Ethernet RX debug aids for FMXCVU19P

## Do NOT read this peripheral's registers from userspace

Reading `0x64100000` (AXI DMA) or `0x64200000` (AXI Ethernet) via `/dev/mem`
**hangs the board permanently** once `eth0` is up. Observed repeatedly; the
board never recovers and needs a power cycle.

Two contributing hazards, both confirmed:

1. Undocumented offsets. Only a subset of each window decodes. A load from an
   unimplemented offset never gets an AXI response, and TileLink has no
   timeout, so the CPU wedges. The Linux header
   (`drivers/net/ethernet/xilinx/xilinx_axienet.h`) documents only:
   `0x00 RAF, 0x04 TPF, 0x08 IFGP, 0x0C IS, 0x10 IP, 0x14 IE, 0x18 TTAG,
   0x1C RTAG, 0x20 UAWL, 0x24 UAWU, 0x30 PPST, 0x400 RCW0, 0x404 RCW1,
   0x408 TC, 0x40C FCC, 0x410 EMMC, 0x414 PHYC, 0x4F8 ID, 0x500 MDIO_MC,
   0x504 MCR, 0x508 MWD, 0x50C MRD`. Everything else is unknown.
2. Python register access is byte-wise. `ctypes`/`struct` are implemented as a
   4-byte `memcpy`, and glibc copies byte-at-a-time below `OP_T_THRES`, so a
   "32-bit" access is really four byte accesses. On this DMA that returned
   byte 0 only and produced a long chain of wrong conclusions.

`mdioread.c` here has the same hazard (it reads the Ethernet window) and is
kept only for reference. It does use proper 32-bit `volatile` accesses.

## Use kernel-side data instead — safe, and sufficient

    ip link set eth0 down && ip link set eth0 up
    dmesg | tail -20
    ip -s link show eth0
    grep eth0 /proc/interrupts
    grep . /sys/class/net/eth0/statistics/*

The driver prints `S2MM_DMASR` verbatim on error
(`netdev_err(ndev, "DMA Rx error 0x%x\n", status)`), which is how the
`0x15019` / `DMAIntErr` diagnosis was made. That is the register dump, for
free, with no hazard.

## Files

- `eth_rx_debug.xdc` — `MARK_DEBUG` list for the RX path (alternative to the
  BD automation in `fpga-shells/xilinx/fmxcvu19p/tcl/add_vela_eth_ila.tcl`;
  use one, not both)
- `mdioread.c` / `build/mdioread` — reference only, see warning above
