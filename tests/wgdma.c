// Device-side WorldGuard test for the WID-marked DMA (WGDMA).
//
// Four cases, run in M-mode (mlwid resets to nWorlds-1 = 31 = the checkers'
// mwid, so this program is the trusted/monitor world and may program them):
//
//   1. registered WID   -> transfer completes and the data really lands
//   2. unregistered WID -> checker denies, DMA reports ERROR
//   3. address outside any slot -> denied (no slot hits; default is wid==mwid)
//   4. protection relaxed for that WID -> the same transfer now passes
//
// Case 4 is the control: only the slot's perm bit changes between 2 and 4.

#include <stdint.h>
#include <stdio.h>
#include "mmio.h"

/* ---- DMA ------------------------------------------------------------- */
#define DMA_BASE      0x02030000UL
#define DMA_SRC_LO    (DMA_BASE + 0x00)
#define DMA_SRC_HI    (DMA_BASE + 0x04)
#define DMA_DST_LO    (DMA_BASE + 0x08)
#define DMA_DST_HI    (DMA_BASE + 0x0c)
#define DMA_LEN       (DMA_BASE + 0x10)
#define DMA_CTRL      (DMA_BASE + 0x14)
#define DMA_STATUS    (DMA_BASE + 0x18)
#define DMA_BYTES     (DMA_BASE + 0x1c)

#define ST_BUSY       (1u << 0)
#define ST_DONE       (1u << 1)
#define ST_ERROR      (1u << 2)
#define ST_CORRUPT    (1u << 3)
#define ST_ADDRERR    (1u << 4)

/* ---- the DMA's WGMarker ---------------------------------------------- */
#define MK_BASE       0x02040000UL
#define MK_WID        (MK_BASE + 0x08)
#define MK_LOCK       (MK_BASE + 0x0c)

/* ---- memory-port checker --------------------------------------------- */
/* wgcReg(i) sits at slotBase + 0x20*(i+1); slotBase itself (0x20) is the
   read-only fallback slot[0], so the first programmable slot is at 0x40. */
/* Which checker sits on the memory path depends on which mem port the build
   uses: the VCU118 config has only the TL one at 0x6000000, while the
   simulation harness brings up the AXI4 port and its checker at 0x2060000.
   Program every checker present so the test is identical on both. */
static const unsigned long WGC_BASES[] = { 0x06000000UL, 0x02060000UL };
#define WGC_N_BASES   (sizeof(WGC_BASES) / sizeof(WGC_BASES[0]))
#define WGC_BASE      WGC_BASES[0]
#define WGC_NSLOTS(b) ((b) + 0x08)
#define WGC_ERRWIDRW(b) ((b) + 0x10)
#define WGC_ERRADDR(b)  ((b) + 0x18)
#define WGC_SLOT(b,i) ((b) + 0x20 + 0x20 * ((i) + 1))
#define WGC_ADDR(b,i) (WGC_SLOT(b,i) + 0x00)
#define WGC_PERM(b,i) (WGC_SLOT(b,i) + 0x08)
#define WGC_CFG(b,i)  (WGC_SLOT(b,i) + 0x10)

/* WGCConfig packs as: a[1:0], res[7:2], er=8, ew=9, ir=10, iw=11, l=31.
   er/ew are what turn a detected violation into an actual bus error -- with
   them clear the checker still refuses the access but the master is never
   told, so the DMA would report success on a transfer that moved nothing. */
#define CFG_A_OFF     0u
#define CFG_A_NAPOT   3u
#define CFG_ER        (1u << 8)
#define CFG_EW        (1u << 9)
#define CFG_NAPOT_ERR (CFG_A_NAPOT | CFG_ER | CFG_EW)

/* A transaction that hits no slot at all reports through the read-only
   fallback slot[0] (Checker: io.cfg := Mux(anyHit, cfgOut, io.slot0Cfg)),
   so case 3 needs er/ew set *there*, not on the slot under test. slot[0]
   lives at slotBase itself; only its er/ew/ir/iw/l bits are writable. */
#define WGC_SLOT0_CFG(b) ((b) + 0x20 + 0x10)

/* The checker is built with lgAlign = 6 (64-byte granularity), so its addr
   field is the physical address shifted right by 6. */
#define WGC_LGALIGN   6

/* ---- L2 ---------------------------------------------------------------- */
#define L2_BASE       0x02010000UL
#define L2_FLUSH64    (L2_BASE + 0x200)
#define L2_FLUSHWID   (L2_BASE + 0x218)
#define L2_DBG_OUTA   (L2_BASE + 0x318)

/* ---- test layout ------------------------------------------------------- */
/* The program image, its heap/stack and the HTIF mailbox all live in the
   first megabyte of DRAM. fesvr reaches tohost/fromhost over TSI, which has
   no WGMarker, so those accesses arrive with a WID that is not mwid; with no
   slot programmed the checker's default (wid==mwid) refuses them and the very
   first printf would hang forever. Open this window to every world before
   printing anything. It stops exactly at REGION_BASE, so it never overlaps -
   and never silently widens - the slot under test. */
#define PROG_BASE     0x80000000UL
#define PROG_SIZE     0x100000UL
#define PROG_SLOT     1

#define REGION_BASE   0x80100000UL
#define REGION_SIZE   4096UL
#define SRC_BUF       (REGION_BASE)
#define DST_BUF       (REGION_BASE + 2048)
#define OUTSIDE_BUF   0x80200000UL     /* no slot covers this */
#define XFER_LEN      64u              /* 8 beats of 8 bytes */
#define LINE          64UL

#define DMA_WID_OK    1
#define DMA_WID_BAD   2

static int failures = 0;

static void expect(const char *name, int cond, const char *detail)
{
    if (cond) {
        printf("  PASS  %s\n", name);
    } else {
        printf("  FAIL  %s (%s)\n", name, detail);
        failures++;
    }
}

/* Push a range out of L2 so DRAM is authoritative and no stale line can
   satisfy a request before it reaches the checker on the memory port.
   The L2 issues each flush under the WID in its FlushWid register: a flush
   whose WID does not own the dirty line takes the cross-WID onlyTagHit path
   and does NOT write it back. Lines here are dirtied by the monitor (31) and
   by the DMA under whichever WID its marker held, so sweep all of them. */
static const int FLUSH_WIDS[] = { 31, DMA_WID_OK, DMA_WID_BAD };
#define N_FLUSH_WIDS (sizeof(FLUSH_WIDS) / sizeof(FLUSH_WIDS[0]))

static void l2_flush_range(uint64_t base, uint64_t bytes)
{
    for (unsigned k = 0; k < N_FLUSH_WIDS; k++) {
        reg_write32(L2_FLUSHWID, (uint32_t)FLUSH_WIDS[k]);
        for (uint64_t a = base; a < base + bytes; a += LINE)
            reg_write64(L2_FLUSH64, a);
    }
    asm volatile ("fence");
}

static uint64_t napot_addr(uint64_t base, uint64_t size)
{
    return (base | ((size >> 1) - 1)) >> WGC_LGALIGN;
}

/* perm is two bits per world: bit 2w = read, bit 2w+1 = write. */
static uint64_t perm_rw(int wid)
{
    return (1ULL << (wid * 2)) | (1ULL << (wid * 2 + 1));
}

static void wgc_program(int slot, uint64_t base, uint64_t size, uint64_t perm)
{
    for (unsigned k = 0; k < WGC_N_BASES; k++) {
        unsigned long b = WGC_BASES[k];
        reg_write32(WGC_CFG(b, slot), CFG_A_OFF);   /* disarm while editing */
        reg_write64(WGC_ADDR(b, slot), napot_addr(base, size));
        reg_write64(WGC_PERM(b, slot), perm);
        reg_write32(WGC_CFG(b, slot), CFG_NAPOT_ERR);
    }
    asm volatile ("fence");
}

static void marker_set_wid(int wid)
{
    reg_write32(MK_WID, (uint32_t)wid);
    asm volatile ("fence");
}

/* Returns the STATUS word, or 0xFFFFFFFF if the engine never reported DONE. */
static uint32_t dma_run(uint64_t src, uint64_t dst, uint32_t len)
{
    reg_write32(DMA_SRC_LO, (uint32_t)src);
    reg_write32(DMA_SRC_HI, (uint32_t)(src >> 32));
    reg_write32(DMA_DST_LO, (uint32_t)dst);
    reg_write32(DMA_DST_HI, (uint32_t)(dst >> 32));
    reg_write32(DMA_LEN, len);
    reg_write32(DMA_CTRL, 1);

    for (long i = 0; i < 100000; i++) {
        uint32_t s = reg_read32(DMA_STATUS);
        if (s & ST_DONE)
            return s;
    }
    return 0xFFFFFFFFu;
}

static void fill_src(uint64_t seed)
{
    for (unsigned i = 0; i < XFER_LEN / 8; i++)
        reg_write64(SRC_BUF + i * 8, seed + i);
    for (unsigned i = 0; i < XFER_LEN / 8; i++)
        reg_write64(DST_BUF + i * 8, 0);
    l2_flush_range(SRC_BUF, XFER_LEN);
    l2_flush_range(DST_BUF, XFER_LEN);
}

static int dst_matches(uint64_t seed)
{
    l2_flush_range(DST_BUF, XFER_LEN);
    for (unsigned i = 0; i < XFER_LEN / 8; i++)
        if (reg_read64(DST_BUF + i * 8) != seed + i)
            return 0;
    return 1;
}

static int dst_untouched(void)
{
    l2_flush_range(DST_BUF, XFER_LEN);
    for (unsigned i = 0; i < XFER_LEN / 8; i++)
        if (reg_read64(DST_BUF + i * 8) != 0)
            return 0;
    return 1;
}

int main(void)
{
    /* Must come before the first printf -- see PROG_BASE above. */
    wgc_program(PROG_SLOT, PROG_BASE, PROG_SIZE, ~0ULL);

    /* Make "hit no slot" raise a bus error too (case 3). */
    for (unsigned k = 0; k < WGC_N_BASES; k++)
        reg_write32(WGC_SLOT0_CFG(WGC_BASES[k]), CFG_ER | CFG_EW);

    printf("=== WGDMA device-side WorldGuard test ===\n");
    printf("checker nslots = %u\n", reg_read32(WGC_NSLOTS(WGC_BASE)));

    /* Slot 0 of the programmable array covers the test region and, to start
       with, grants read+write only to DMA_WID_OK. */
    wgc_program(0, REGION_BASE, REGION_SIZE, perm_rw(DMA_WID_OK));
    printf("slot0: base=0x%lx size=%lu perm=0x%lx (wid %d rw)\n",
           REGION_BASE, REGION_SIZE, perm_rw(DMA_WID_OK), DMA_WID_OK);

    /* --- case 1: registered WID -------------------------------------- */
    printf("\n[1] registered WID (%d) should reach the data\n", DMA_WID_OK);
    fill_src(0x1100);
    marker_set_wid(DMA_WID_OK);
    uint32_t outa0 = reg_read32(L2_DBG_OUTA);
    uint32_t s1 = dma_run(SRC_BUF, DST_BUF, XFER_LEN);
    uint32_t outa1 = reg_read32(L2_DBG_OUTA);
    printf("  status=0x%08x bytes=%u  (L2 outer-A fires: %u)\n",
           s1, reg_read32(DMA_BYTES), outa1 - outa0);
    expect("completes", s1 != 0xFFFFFFFFu, "never reported DONE");
    expect("no error", (s1 & ST_ERROR) == 0, "ERROR set");
    expect("data arrived", dst_matches(0x1100), "destination mismatch");

    /* --- case 2: unregistered WID ------------------------------------ */
    printf("\n[2] unregistered WID (%d) should be refused\n", DMA_WID_BAD);
    fill_src(0x2200);
    marker_set_wid(DMA_WID_BAD);
    uint32_t s2 = dma_run(SRC_BUF, DST_BUF, XFER_LEN);
    printf("  status=0x%08x  checker errwid=0x%08x erraddr=0x%08x\n",
           s2, reg_read32(WGC_ERRWIDRW(WGC_BASE)), reg_read32(WGC_ERRADDR(WGC_BASE)));
    expect("completes", s2 != 0xFFFFFFFFu, "never reported DONE");
    expect("error reported", (s2 & ST_ERROR) != 0, "ERROR not set");
    expect("no data leaked", dst_untouched(), "destination was modified");

    /* --- case 3: address outside every slot -------------------------- */
    printf("\n[3] address outside the region should be refused\n");
    fill_src(0x3300);
    marker_set_wid(DMA_WID_OK);
    uint32_t s3 = dma_run(SRC_BUF, OUTSIDE_BUF, XFER_LEN);
    printf("  status=0x%08x  checker erraddr=0x%08x\n",
           s3, reg_read32(WGC_ERRADDR(WGC_BASE)));
    expect("completes", s3 != 0xFFFFFFFFu, "never reported DONE");
    expect("error reported", (s3 & ST_ERROR) != 0, "ERROR not set");

    /* --- case 4: control, protection relaxed ------------------------- */
    printf("\n[4] control: grant WID %d and retry case 2\n", DMA_WID_BAD);
    wgc_program(0, REGION_BASE, REGION_SIZE,
                perm_rw(DMA_WID_OK) | perm_rw(DMA_WID_BAD));
    fill_src(0x4400);
    marker_set_wid(DMA_WID_BAD);
    uint32_t s4 = dma_run(SRC_BUF, DST_BUF, XFER_LEN);
    printf("  status=0x%08x bytes=%u\n", s4, reg_read32(DMA_BYTES));
    expect("completes", s4 != 0xFFFFFFFFu, "never reported DONE");
    expect("no error", (s4 & ST_ERROR) == 0, "ERROR set");
    expect("data arrived", dst_matches(0x4400), "destination mismatch");

    /* --- extra: the 64-bit address guard ----------------------------- */
    printf("\n[5] extra: address above the bus width is rejected\n");
    marker_set_wid(DMA_WID_OK);
    uint32_t s5 = dma_run(0x1ULL << 40, DST_BUF, XFER_LEN);
    printf("  status=0x%08x\n", s5);
    expect("ADDRERR reported", (s5 != 0xFFFFFFFFu) && (s5 & ST_ADDRERR),
           "ADDRERR not set");

    printf("\n=== %s (%d failure(s)) ===\n",
           failures ? "FAILED" : "ALL CASES PASSED", failures);
    return failures;
}
