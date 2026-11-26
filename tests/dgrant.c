// Per-beat observation of the GrantData that comes back for a refused load.
//
// Configuration: WGRocket32EvictGuardConfig (WorldGuard Rocket, single core,
// L2 cross-WID eviction guard ON). Observation only -- the RTL carries printfs
// gated on +dcache_obs_page; nothing in the datapath is changed.
//
// The region under test (R) is admitted for WID e alone. M-mode writes the
// pattern as WID e; S-mode reads it as WID x and is refused. What this file
// varies is where the line sits when that refused read happens, and whether
// the matching slot is configured to raise a bus error.
//
//   G1   R in no cache at all                     slot raises error (er/ew = 1)
//   G2a  R in L2 as WID e, and in L1 (dirty)      slot raises error
//   G2b  R in L2 as WID e only, not in L1         slot raises error
//   G3   same state as G2b, quiet slot            slot raises NO error (er/ew = 0)
//
// Address map (memory-port checker slots):
//   slot 1  0x8000_0000 + 2 MiB  every WID   program, stack, HTIF, sweep buffer
//   slot 0  0x8020_0000 + 4 KiB  WID e only  the region under test

#include <stdint.h>
#include <stdarg.h>

/* ---- output ------------------------------------------------------------- */
/* Not stdio: on these WorldGuard configs the first HTIF syscall comes back as
   "bad syscall #0" -- fesvr reads the syscall argument block (on the stack) as
   zero, although the checker window covers it and no WGCOBS_DENY is logged.
   The UART needs no HTIF, and its divisor resets to what the harness expects,
   so the program only has to enable TX. Exit is a bare tohost write, which
   carries its payload in the word itself and needs no argument block. */
#define UART_BASE   0x10020000UL
#define UART_TXDATA (UART_BASE + 0x00)
#define UART_TXCTRL (UART_BASE + 0x08)
#define rd32(a) (*(volatile uint32_t *)(a))

extern volatile uint64_t tohost;

static void uart_init(void) { *(volatile uint32_t *)UART_TXCTRL = 1u; }
static void uart_putc(char c) {
    while (rd32(UART_TXDATA) & (1u << 31)) { }
    *(volatile uint32_t *)UART_TXDATA = (uint8_t)c;
}
static void up(const char *s) { while (*s) uart_putc(*s++); }
static void uh(uint64_t v) {
    up("0x");
    int i = 60; while (i > 0 && ((v >> i) & 0xf) == 0) i -= 4;
    for (; i >= 0; i -= 4) uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}
static void ud(uint64_t v) {
    char b[24]; int n = 0;
    if (!v) { uart_putc('0'); return; }
    while (v) { b[n++] = '0' + (v % 10); v /= 10; }
    while (n) uart_putc(b[--n]);
}

#define R_BASE   0x80200000UL
#define R_SIZE   4096UL
#define PATTERN  0xdeadbeefcafef00dULL

#define PROG_BASE  0x80000000UL
#define PROG_SIZE  0x200000UL            /* 2 MiB: program + sweep buffer */
#define SWEEP_BASE 0x80100000UL
#define SWEEP_SIZE (256 * 1024UL)        /* >> 16 KiB L1 D, inside PROG window */

#define WID_E 1                          /* owns R */
#define WID_X 0                          /* the refused one */

#define CAUSE_ECALL_S 9
#define MSTATUS_MPP_SHIFT 11

/* ---- memory-port checker ------------------------------------------------ */
/* wgcReg(i) is at slotBase + 0x20*(i+1); slotBase itself is the read-only
   fallback slot[0], so the first programmable slot sits at +0x40.
   Which checker is on the memory path depends on the port the build brings up:
   the VCU118 config has only the TL one at 0x6000000, while this simulation
   harness uses the AXI4 port, whose checker is at 0x2060000. Program both. */
static const unsigned long WGC_BASES[] = { 0x06000000UL, 0x02060000UL };
#define WGC_N_BASES   (sizeof(WGC_BASES) / sizeof(WGC_BASES[0]))
#define WGC_SLOT(b,i) ((b) + 0x20 + 0x20 * ((i) + 1))
#define WGC_ADDR(b,i) (WGC_SLOT(b,i) + 0x00)
#define WGC_PERM(b,i) (WGC_SLOT(b,i) + 0x08)
#define WGC_CFG(b,i)  (WGC_SLOT(b,i) + 0x10)
#define WGC_SLOT0_CFG(b) ((b) + 0x20 + 0x10)
#define WGC_LGALIGN   6
#define CFG_A_NAPOT   3u
#define CFG_ER        (1u << 8)
#define CFG_EW        (1u << 9)
#define CFG_NAPOT_ERR (CFG_A_NAPOT | CFG_ER | CFG_EW)   /* denial raises a bus error */
#define CFG_NAPOT_QUIET (CFG_A_NAPOT)                   /* denial raises nothing */

/* ---- L2 ------------------------------------------------------------------ */
#define L2_BASE     0x02010000UL
#define L2_FLUSH64  (L2_BASE + 0x200)
#define L2_FLUSHWID (L2_BASE + 0x218)
#define LINE 64UL

#define rd64(a)     (*(volatile uint64_t *)(a))
#define wr64(a, v)  (*(volatile uint64_t *)(a) = (v))
#define wr32(a, v)  (*(volatile uint32_t *)(a) = (uint32_t)(v))

#define csrr_mstatus()  ({ uint64_t v; asm volatile ("csrr %0, mstatus" : "=r"(v)); v; })
#define csrw_mstatus(v) asm volatile ("csrw mstatus, %0" :: "r"(v))

static inline void set_mwid(uint64_t w)  { asm volatile ("csrw 0x391, %0" :: "r"(w)); asm volatile ("fence" ::: "memory"); }
static inline void set_mlwid(uint64_t w) { asm volatile ("csrw 0x390, %0" :: "r"(w)); asm volatile ("fence" ::: "memory"); }

static volatile uint64_t trap_count, trap_cause, trap_tval;
static volatile uint64_t load_result;    /* what S-mode's load left in the reg */
uint64_t m_resume;

#define TRAP_LOG_N 6
static volatile uint64_t tlog_cause[TRAP_LOG_N], tlog_epc[TRAP_LOG_N], tlog_tval[TRAP_LOG_N];
static volatile uint64_t tlog_n;
#define TRAP_LIMIT 64

extern void trap_entry(void);
extern void enter_smode(void (*fn)(void));

uint64_t handle_trap(uint64_t cause, uint64_t epc, uint64_t tval)
{
    if (cause == CAUSE_ECALL_S) {
        uint64_t ms = csrr_mstatus();
        ms &= ~(3UL << MSTATUS_MPP_SHIFT);
        ms |=  (3UL << MSTATUS_MPP_SHIFT);     /* MPP = M */
        csrw_mstatus(ms);
        return m_resume;
    }
    trap_count++; trap_cause = cause; trap_tval = tval;
    if (tlog_n < TRAP_LOG_N) {
        tlog_cause[tlog_n] = cause; tlog_epc[tlog_n] = epc; tlog_tval[tlog_n] = tval;
        tlog_n++;
    }
    if (trap_count >= TRAP_LIMIT) {
        uint64_t ms = csrr_mstatus();
        ms &= ~(3UL << MSTATUS_MPP_SHIFT);
        ms |=  (3UL << MSTATUS_MPP_SHIFT);
        csrw_mstatus(ms);
        return m_resume;
    }
    return epc + 4;
}

/* ---- setup --------------------------------------------------------------- */
static uint64_t napot(uint64_t base, uint64_t size) { return (base | ((size >> 1) - 1)) >> WGC_LGALIGN; }
static uint64_t perm_rw(int wid) { return (1ULL << (wid * 2)) | (1ULL << (wid * 2 + 1)); }

static void wgc_program(int slot, uint64_t base, uint64_t size, uint64_t perm, uint32_t cfg)
{
    for (unsigned k = 0; k < WGC_N_BASES; k++) {
        unsigned long b = WGC_BASES[k];
        wr32(WGC_CFG(b, slot), 0);
        wr64(WGC_ADDR(b, slot), napot(base, size));
        wr64(WGC_PERM(b, slot), perm);
        wr32(WGC_CFG(b, slot), cfg);
    }
    asm volatile ("fence" ::: "memory");
}

/* Change only the cfg of an already-programmed slot (loud <-> quiet). */
static void wgc_set_cfg(int slot, uint32_t cfg)
{
    for (unsigned k = 0; k < WGC_N_BASES; k++) wr32(WGC_CFG(WGC_BASES[k], slot), cfg);
    asm volatile ("fence" ::: "memory");
}

static void pmp_open_everything(void)
{
    /* TOR, not NAPOT: pmpaddr is the address >> 2. One TOR entry whose bound is
       the top of the space covers [0, top) with RWX, so PMP never refuses. */
    asm volatile ("csrw pmpaddr0, %0" :: "r"(~0UL >> 2));
    asm volatile ("csrw pmpcfg0, %0" :: "r"((uint64_t)((1UL << 3) | 7UL)));
    asm volatile ("sfence.vma" ::: "memory");
}

static void l2_flush_range(uint64_t base, uint64_t bytes)
{
    const int wids[] = { 31, WID_E, WID_X };
    for (unsigned k = 0; k < sizeof(wids)/sizeof(wids[0]); k++) {
        wr32(L2_FLUSHWID, wids[k]);
        for (uint64_t a = base; a < base + bytes; a += LINE) wr64(L2_FLUSH64, a);
    }
    asm volatile ("fence" ::: "memory");
}

static void sweep_l1(void)
{
    volatile uint64_t sink = 0;
    for (uint64_t a = SWEEP_BASE; a < SWEEP_BASE + SWEEP_SIZE; a += LINE) sink += rd64(a);
    (void)sink;
    asm volatile ("fence" ::: "memory");
}

/* ---- the refused access -------------------------------------------------- */
static void smode_load(void)
{
    uint64_t v;
    asm volatile ("ld %0, 0(%1)" : "=r"(v) : "r"(R_BASE) : "memory");
    load_result = v;
    asm volatile ("ecall");
}

static void run_case(const char *name)
{
    trap_count = 0; trap_cause = ~0ULL; trap_tval = 0; tlog_n = 0; load_result = 0;
    set_mlwid(WID_X);
    up("\n["); up(name); up("] S-mode (WID "); ud(WID_X); up(") load from "); uh(R_BASE); up("\n");
    enter_smode(smode_load);
    up("["); up(name); up("] traps="); ud(trap_count);
    up(" cause="); ud(trap_cause); up(" tval="); uh(trap_tval);
    up(" load_result="); uh(load_result);
    if (trap_count >= TRAP_LIMIT) up("  (hit trap limit)");
    up("\n");
    for (uint64_t i = 0; i < tlog_n; i++) {
        up("    trap["); ud(i); up("] cause="); ud(tlog_cause[i]);
        up(" epc="); uh(tlog_epc[i]); up(" tval="); uh(tlog_tval[i]); up("\n");
    }

    set_mwid(WID_E);
    uint64_t back = rd64(R_BASE);
    up("["); up(name); up("] owner(WID "); ud(WID_E); up(") readback = "); uh(back);
    up(back == (uint64_t)PATTERN ? "  (matches pattern)" : "  (DIFFERS)"); up("\n");
}

int main(void)
{
    /* Output goes out over the UART, which is not WID-filtered, so the order
       here no longer matters for visibility. The slots are still programmed
       before anything else so the measured accesses see a settled checker. */
    uart_init();

    asm volatile ("csrw mtvec, %0" :: "r"((uint64_t)trap_entry));
    pmp_open_everything();
    wgc_program(1, PROG_BASE, PROG_SIZE, ~0ULL, CFG_NAPOT_ERR);
    for (unsigned k = 0; k < WGC_N_BASES; k++)
        wr32(WGC_SLOT0_CFG(WGC_BASES[k]), CFG_ER | CFG_EW);
    wgc_program(0, R_BASE, R_SIZE, perm_rw(WID_E), CFG_NAPOT_ERR);

    up("=== denied-GrantData beat dump (guard ON) ===\n");
    up("slot0: R="); uh(R_BASE); up(" size="); ud(R_SIZE);
    up(" perm="); uh(perm_rw(WID_E)); up(" (WID "); ud(WID_E); up(" only) pattern=");
    uh(PATTERN); up("\n");

    /* Owner establishes the pattern in memory. */
    set_mwid(WID_E);
    wr64(R_BASE, PATTERN);
    asm volatile ("fence" ::: "memory");
    up("owner wrote pattern, readback="); uh(rd64(R_BASE)); up("\n");

    /* ---- G1: not in any cache ------------------------------------------- */
    /* Push the line out of L1, then write it back from L2 to DRAM and drop it.
       Do NOT touch R as owner afterwards, or it refills. */
    set_mwid(WID_E);
    sweep_l1();
    l2_flush_range(R_BASE, LINE);
    run_case("G1 no-cache");

    /* ---- G2a: in L2 as WID e, and in L1 (dirty) -------------------------- */
    set_mwid(WID_E);
    sweep_l1();
    l2_flush_range(R_BASE, LINE);
    wr64(R_BASE, PATTERN);                 /* dirty in L1, allocated in L2 */
    asm volatile ("fence" ::: "memory");
    run_case("G2a L2+L1-dirty");

    /* ---- G2b: in L2 as WID e only ---------------------------------------- */
    set_mwid(WID_E);
    sweep_l1();
    l2_flush_range(R_BASE, LINE);
    (void)rd64(R_BASE);                    /* allocate clean in L1 and L2 */
    asm volatile ("fence" ::: "memory");
    sweep_l1();                            /* evict from L1, leave it in L2 */
    run_case("G2b L2-only");

    /* ---- G3a: quiet slot, line in no cache -------------------------------- */
    /* With the line absent from L2 the request reaches the memory-port checker,
       so what comes back is that checker's own deny message. */
    set_mwid(WID_E);
    sweep_l1();
    l2_flush_range(R_BASE, LINE);
    wgc_set_cfg(0, CFG_NAPOT_QUIET);       /* quiet: er = ew = 0 */
    up("\n(slot0 cfg -> "); uh(CFG_NAPOT_QUIET); up(", quiet)\n");
    run_case("G3a quiet no-cache");

    /* ---- G3b: quiet slot, line in L2 as WID e ---------------------------- */
    /* Here the L2 answers first, so the quiet cfg never reaches the memory-port
       checker. Recorded to show which of the two produced the response. */
    set_mwid(WID_E);
    sweep_l1();
    l2_flush_range(R_BASE, LINE);
    (void)rd64(R_BASE);
    asm volatile ("fence" ::: "memory");
    sweep_l1();
    run_case("G3b quiet L2-only");
    wgc_set_cfg(0, CFG_NAPOT_ERR);

    up("\n=== done ===\n");
    /* Exit without a syscall: tohost carries its payload in the word itself. */
    tohost = 1;
    for (;;) { }
    return 0;
}
