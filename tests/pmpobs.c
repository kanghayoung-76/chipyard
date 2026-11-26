// Configuration A: what a stock Rocket's L1 DCache does with a load that PMP
// refuses. Observation only -- the RTL is unchanged; DCache.scala carries a
// printf gated on +dcache_obs_page.
//
// PMP entry 0 covers region R with no permissions, entry 1 opens everything
// else. An unlocked PMP entry constrains S/U only, so M-mode still writes the
// pattern; S-mode is the one that gets refused.
//
//   A1  pattern written by M-mode immediately before, so R's line is in L1
//   A2  L1 swept by streaming a buffer larger than it, so R's line is not
//
// A3 (page-table walk) is not in this file.

#include <stdint.h>
#include <stdio.h>

#define R_BASE   0x80200000UL
#define R_SIZE   4096UL
#define PATTERN  0xdeadbeefcafef00dULL

#define L1_SWEEP_BASE 0x80300000UL
#define L1_SWEEP_SIZE (256 * 1024UL)   /* >> 16 KiB L1 D */

#define CAUSE_LOAD_ACCESS  5
#define CAUSE_ECALL_S      9

#define PRV_S 1UL
#define MSTATUS_MPP_SHIFT 11

static volatile uint64_t trap_count;
static volatile uint64_t trap_cause;
static volatile uint64_t trap_tval;
static volatile uint64_t load_result;   /* what S-mode's load left in the reg */
uint64_t m_resume;                      /* set by enter_smode, used by handle_trap */

#define csrw(reg, val) asm volatile ("csrw " #reg ", %0" :: "r"(val))
#define csrr(reg) ({ uint64_t __v; asm volatile ("csrr %0, " #reg : "=r"(__v)); __v; })

extern void trap_entry(void);
extern void enter_smode(void (*fn)(void));

/* ---- M-mode trap handler ------------------------------------------------ */
/* Records the cause and steps over the faulting instruction. An ecall from
   S-mode is how the S-mode body hands control back. */
uint64_t handle_trap(uint64_t cause, uint64_t epc, uint64_t tval)
{
    if (cause == CAUSE_ECALL_S) {
        /* The S-mode body is finished. A trap from S left MPP = S, which would
           send mret back down; force MPP = M and resume in enter_smode. */
        uint64_t ms = csrr(mstatus);
        ms &= ~(3UL << MSTATUS_MPP_SHIFT);
        ms |=  (3UL << MSTATUS_MPP_SHIFT);   /* MPP = M */
        csrw(mstatus, ms);
        return m_resume;
    }
    trap_count++;
    trap_cause = cause;
    trap_tval  = tval;
    /* Step over the faulting instruction; everything here is 4-byte encoded. */
    return epc + 4;
}

/* ---- PMP ---------------------------------------------------------------- */
/* pmpcfg byte: bit0 R, bit1 W, bit2 X, bits[4:3] A (3 = NAPOT), bit7 L. */
#define PMP_A_NAPOT (3UL << 3)
#define PMP_R 1UL
#define PMP_W 2UL
#define PMP_X 4UL

static uint64_t napot(uint64_t base, uint64_t size)
{
    return (base | ((size >> 1) - 1)) >> 2;
}

static void pmp_setup(void)
{
    /* entry 0: R, no permissions. entry 1: the whole space, RWX.
       Lowest matching index wins, so R stays refused for S/U. */
    csrw(pmpaddr0, napot(R_BASE, R_SIZE));
    csrw(pmpaddr1, napot(0UL, 1UL << 56));
    uint64_t cfg = (PMP_A_NAPOT | 0UL)                    /* entry 0 */
                 | ((PMP_A_NAPOT | PMP_R | PMP_W | PMP_X) << 8); /* entry 1 */
    csrw(pmpcfg0, cfg);
    asm volatile ("sfence.vma" ::: "memory");
}

/* ---- helpers ------------------------------------------------------------ */
static void sweep_l1(void)
{
    volatile uint64_t sink = 0;
    for (uint64_t a = L1_SWEEP_BASE; a < L1_SWEEP_BASE + L1_SWEEP_SIZE; a += 64)
        sink += *(volatile uint64_t *)a;
    (void)sink;
    asm volatile ("fence" ::: "memory");
}

/* The S-mode body: one load from R, then hand back to M-mode. The result is
   stored so we can see what the register ended up holding. */
static void smode_load(void)
{
    uint64_t v;
    asm volatile ("ld %0, 0(%1)" : "=r"(v) : "r"(R_BASE) : "memory");
    load_result = v;
    asm volatile ("ecall");
}

static void run_case(const char *name)
{
    trap_count = 0; trap_cause = ~0ULL; trap_tval = 0; load_result = 0;
    printf("\n[%s] entering S-mode, load from 0x%lx\n", name, R_BASE);
    enter_smode(smode_load);
    printf("[%s] traps=%lu cause=%lu tval=0x%lx load_result=0x%lx\n",
           name, trap_count, trap_cause, trap_tval, load_result);
}

int main(void)
{
    printf("=== Config A: stock Rocket, PMP ===\n");
    csrw(mtvec, (uint64_t)trap_entry);
    pmp_setup();

    /* M-mode writes the pattern. PMP entry 0 is unlocked, so M is unaffected. */
    *(volatile uint64_t *)R_BASE = PATTERN;
    asm volatile ("fence" ::: "memory");
    printf("pattern 0x%llx written to 0x%lx (M-mode readback 0x%lx)\n",
           (unsigned long long)PATTERN, R_BASE, *(volatile uint64_t *)R_BASE);

    /* A1: the line is in L1 right now. */
    run_case("A1 line-in-L1");

    /* A2: push it out of L1 first. */
    sweep_l1();
    run_case("A2 line-not-in-L1");

    printf("\n=== done ===\n");
    return 0;
}
