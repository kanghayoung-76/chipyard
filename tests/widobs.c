// Configuration B: what the WorldGuard Rocket does with a load the checker
// refuses. Same shape as pmpobs.c (configuration A), with WID in place of PMP.
// Observation only -- RTL unchanged apart from printfs.
//
// PMP is left wide open; the checker slot on R admits WID e alone. The core's
// WID comes from CSR mwid (0x391) in M-mode and mlwid (0x390) in S-mode, so
// M-mode writes the pattern as WID e and S-mode reads it as WID 0.
//
//   B1d  R's line dirty in L1 under WID e
//   B1c  R's line clean in L1 under WID e
//   B2   R's line not in L1 at all
//
// Address map (checker slots):
//   slot 1  0x8000_0000 + 2 MiB  every WID   program, stack, HTIF, sweep buffer
//   slot 0  0x8020_0000 + 4 KiB  WID e only  the region under test

#include <stdint.h>
#include <stdio.h>

#define R_BASE   0x80200000UL
#define R_SIZE   4096UL
#define PATTERN  0xdeadbeefcafef00dULL

#define PROG_BASE 0x80000000UL
#define PROG_SIZE 0x200000UL           /* 2 MiB: program + sweep buffer */
#define SWEEP_BASE 0x80100000UL
#define SWEEP_SIZE (256 * 1024UL)      /* >> 16 KiB L1 D, inside PROG window */

#define WID_E 1                        /* owns R */
#define WID_X 0                        /* the refused one */

#define CAUSE_ECALL_S 9
#define MSTATUS_MPP_SHIFT 11

/* ---- memory-port checker ------------------------------------------------ */
/* wgcReg(i) is at slotBase + 0x20*(i+1); slotBase itself is the read-only
   fallback slot[0], so the first programmable slot sits at +0x40. */
/* Which checker is on the memory path depends on which port the build brings
   up: the VCU118 config has only the TL one at 0x6000000, while this
   simulation harness uses the AXI4 port, whose checker is at 0x2060000.
   Program every checker present so the test does not depend on that. */
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
#define CFG_NAPOT_ERR (CFG_A_NAPOT | CFG_ER | CFG_EW)

/* ---- L2 ------------------------------------------------------------------ */
#define L2_BASE     0x02010000UL
#define L2_FLUSH64  (L2_BASE + 0x200)
#define L2_FLUSHWID (L2_BASE + 0x218)
#define LINE 64UL

#define rd64(a)      (*(volatile uint64_t *)(a))
#define wr64(a, v)   (*(volatile uint64_t *)(a) = (v))
#define wr32(a, v)   (*(volatile uint32_t *)(a) = (uint32_t)(v))

#define csrw_imm(num, val) asm volatile ("csrw " #num ", %0" :: "r"(val))
#define csrr_mstatus() ({ uint64_t v; asm volatile ("csrr %0, mstatus" : "=r"(v)); v; })
#define csrw_mstatus(v) asm volatile ("csrw mstatus, %0" :: "r"(v))

static inline void set_mwid(uint64_t w)  { asm volatile ("csrw 0x391, %0" :: "r"(w)); asm volatile ("fence" ::: "memory"); }
static inline void set_mlwid(uint64_t w) { asm volatile ("csrw 0x390, %0" :: "r"(w)); asm volatile ("fence" ::: "memory"); }

static volatile uint64_t trap_count, trap_cause, trap_tval;
uint64_t m_resume;

/* The S-mode body is one faulting load plus an ecall, so a handful of traps is
   all it may take. Anything more means the handler is not making progress;
   record the first few and abandon the case rather than spin forever. */
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
        /* Give up on this case: return to M-mode at enter_smode's resume point. */
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

static void wgc_program(int slot, uint64_t base, uint64_t size, uint64_t perm)
{
    for (unsigned k = 0; k < WGC_N_BASES; k++) {
        unsigned long b = WGC_BASES[k];
        wr32(WGC_CFG(b, slot), 0);
        wr64(WGC_ADDR(b, slot), napot(base, size));
        wr64(WGC_PERM(b, slot), perm);
        wr32(WGC_CFG(b, slot), CFG_NAPOT_ERR);
    }
    asm volatile ("fence" ::: "memory");
}

static void pmp_open_everything(void)
{
    /* TOR, not NAPOT: pmpaddr is the address >> 2, and reusing napot() (which
       shifts by the checker's lgAlign of 6) got this wrong and left S-mode
       unable to fetch at all. A single TOR entry whose bound is the top of the
       space covers [0, top) with RWX, so PMP never refuses anything. */
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
    (void)v;
    asm volatile ("ecall");
}

static void run_case(const char *name)
{
    trap_count = 0; trap_cause = ~0ULL; trap_tval = 0; tlog_n = 0;
    set_mlwid(WID_X);
    printf("\n[%s] S-mode (WID %d) load from 0x%lx\n", name, WID_X, R_BASE);
    enter_smode(smode_load);
    printf("[%s] traps=%lu cause=%lu tval=0x%lx%s\n", name, trap_count, trap_cause,
           trap_tval, trap_count >= TRAP_LIMIT ? "  (hit trap limit)" : "");
    for (uint64_t i = 0; i < tlog_n; i++)
        printf("    trap[%lu] cause=%lu epc=0x%lx tval=0x%lx\n",
               i, tlog_cause[i], tlog_epc[i], tlog_tval[i]);

    /* Is the line still readable as its owner? */
    set_mwid(WID_E);
    uint64_t back = rd64(R_BASE);
    printf("[%s] owner(WID %d) readback = 0x%lx%s\n", name, WID_E, back,
           back == (uint64_t)PATTERN ? "  (matches pattern)" : "  (DIFFERS)");
}

int main(void)
{
    /* Nothing may print before this: HTIF lives in DRAM and fesvr's accesses
       carry no WID of ours, so with no slot programmed the checker's default
       (wid == mwid) refuses them and the first printf never completes. */
    asm volatile ("csrw mtvec, %0" :: "r"((uint64_t)trap_entry));
    pmp_open_everything();
    wgc_program(1, PROG_BASE, PROG_SIZE, ~0ULL);
    for (unsigned k = 0; k < WGC_N_BASES; k++)
        wr32(WGC_SLOT0_CFG(WGC_BASES[k]), CFG_ER | CFG_EW);
    wgc_program(0, R_BASE, R_SIZE, perm_rw(WID_E));

    printf("=== Config B: WorldGuard Rocket, WID ===\n");
    for (unsigned k = 0; k < WGC_N_BASES; k++) {
        unsigned long b = WGC_BASES[k];
        printf("checker 0x%lx: slot1 addr=0x%lx perm=0x%lx cfg=0x%x | "
               "slot0 addr=0x%lx perm=0x%lx cfg=0x%x\n", b,
               rd64(WGC_ADDR(b,1)), rd64(WGC_PERM(b,1)), *(volatile uint32_t*)WGC_CFG(b,1),
               rd64(WGC_ADDR(b,0)), rd64(WGC_PERM(b,0)), *(volatile uint32_t*)WGC_CFG(b,0));
    }
    printf("expected slot1 addr=0x%lx (PROG 2MiB), slot0 addr=0x%lx (R 4KiB)\n",
           napot(PROG_BASE, PROG_SIZE), napot(R_BASE, R_SIZE));
    printf("slot0: R=0x%lx size=%lu perm=0x%lx (WID %d only)\n",
           R_BASE, R_SIZE, perm_rw(WID_E), WID_E);

    /* Owner writes the pattern: R's line is now dirty in L1 under WID e. */
    set_mwid(WID_E);
    wr64(R_BASE, PATTERN);
    asm volatile ("fence" ::: "memory");
    printf("owner wrote pattern, readback=0x%lx\n", rd64(R_BASE));

    run_case("B1d dirty-in-L1");

    /* Clean in L1: push it out, then read it back so it returns unmodified. */
    set_mwid(WID_E);
    sweep_l1();
    l2_flush_range(R_BASE, LINE);
    (void)rd64(R_BASE);
    asm volatile ("fence" ::: "memory");
    run_case("B1c clean-in-L1");

    /* Not in L1 at all. */
    set_mwid(WID_E);
    sweep_l1();
    run_case("B2 not-in-L1");

    printf("\n=== done ===\n");
    return 0;
}
