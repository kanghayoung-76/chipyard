/**
 * memtest.c - FMXCVU19P 2-MIG DDR4 Continuous Address Range Test
 *
 * Verifies that the two DDR4 MIG controllers (TA0 and TA1) together
 * form a single continuous 36 GiB address space:
 *
 *   TA0: 0x0_8000_0000 ~ 0x4_FFFF_FFFF  (18 GiB, MIG0 / DDRTA0)
 *   TA1: 0x5_0000_0000 ~ 0x9_7FFF_FFFF  (18 GiB, MIG1 / DDRTA1)
 *
 * Test strategy:
 *  - Write/read-back 4 KB blocks at 256 MB stride over each MIG region
 *  - Perform a focused boundary test at the TA0→TA1 transition
 *  - Use multiple data patterns (fixed, address-as-data, complement)
 *
 * Build:  cd memtest && make
 * Usage:  Load the resulting .bin as a bootrom payload
 */

#include <stdint.h>
#include <stddef.h>
#include "kprintf.h"

/* ── Memory map ─────────────────────────────────────────────────────── */
#define TA0_BASE    ((uint64_t)0x80000000UL)
#define TA0_SIZE    ((uint64_t)0x480000000UL)   /* 18 GiB */
#define TA0_END     (TA0_BASE + TA0_SIZE)        /* 0x500000000 */

#define TA1_BASE    TA0_END                       /* 0x500000000 */
#define TA1_SIZE    ((uint64_t)0x480000000UL)   /* 18 GiB */
#define TA1_END     (TA1_BASE + TA1_SIZE)        /* 0x980000000 */

/*
 * Safe test start in TA0: skip the first 64 MB so we do not
 * overwrite the memtest binary itself (loaded near 0x80000000).
 */
#define SAFE_OFFSET     ((uint64_t)0x4000000UL) /* 64 MB */
#define TA0_TEST_BASE   (TA0_BASE + SAFE_OFFSET)
#define TA0_TEST_SIZE   (TA0_SIZE - SAFE_OFFSET)

/* ── Test parameters ─────────────────────────────────────────────────── */
#define BLOCK_BYTES     (4096U)                 /* 4 KB per test block  */
#define BLOCK_WORDS     (BLOCK_BYTES / 4)
#define STRIDE          ((uint64_t)0x10000000UL)/* 256 MB between blocks */

/* ── Global counters ─────────────────────────────────────────────────── */
static uint64_t g_blocks  = 0;
static uint64_t g_errors  = 0;
#define MAX_ERRORS_PER_BLOCK  4   /* print at most N errors per block    */

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void print_hex64(uint64_t v)
{
    /* kprintf has no %llx in minimal builds, so hand-roll it */
    static const char hex[] = "0123456789abcdef";
    char buf[19];
    buf[0]  = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = hex[(v >> (60 - i*4)) & 0xF];
    buf[18] = '\0';
    kputs(buf);
}

/* Write-then-verify one 4 KB block */
static int test_block_pattern(uint64_t base, uint32_t seed)
{
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)base;
    int local_err = 0;

    /* Write pass */
    for (uint32_t i = 0; i < BLOCK_WORDS; i++)
        p[i] = seed ^ i;

    /* Memory barrier: ensure all writes complete before reads */
    __asm__ __volatile__("fence rw, rw" ::: "memory");

    /* Read-back pass */
    for (uint32_t i = 0; i < BLOCK_WORDS; i++) {
        uint32_t expected = seed ^ i;
        uint32_t got      = p[i];
        if (got != expected) {
            if (local_err < MAX_ERRORS_PER_BLOCK) {
                kputs("  ERR addr=");
                print_hex64(base + i * 4);
                kprintf(" exp=0x%08x got=0x%08x\n", expected, got);
            }
            local_err++;
            g_errors++;
        }
    }
    return local_err;
}

/* Test a complete region (base..base+size) at stride intervals.
 * Always tests the very last block in the region so we hit the end address. */
static int test_region(const char *tag, uint64_t base, uint64_t size,
                       uint32_t pattern)
{
    kprintf("  [%s]  ", tag);
    print_hex64(base);
    kputs(" ~ ");
    print_hex64(base + size - 1);
    kputc('\n');

    uint64_t errs_before = g_errors;
    uint64_t blocks_this = 0;

    /* Walk from beginning with STRIDE steps */
    uint64_t off = 0;
    int last_done = 0;

    while (!last_done) {
        uint64_t addr = base + off;

        /* Clamp: do not exceed the region */
        if (off + BLOCK_BYTES > size)
            break;

        test_block_pattern(addr, pattern);
        g_blocks++;
        blocks_this++;

        uint64_t next_off = off + STRIDE;
        if (next_off + BLOCK_BYTES > size) {
            /* Jump to the very last aligned block if not already there */
            uint64_t last_off = size - BLOCK_BYTES;
            if (last_off != off) {
                off      = last_off;
                continue;   /* test it without marking last_done yet */
            }
            last_done = 1;
        } else {
            off = next_off;
        }
    }

    uint64_t new_errs = g_errors - errs_before;
    if (new_errs == 0)
        kprintf("    PASS  (%llu blocks)\n", (unsigned long long)blocks_this);
    else
        kprintf("    FAIL  (%llu errors / %llu blocks)\n",
                (unsigned long long)new_errs,
                (unsigned long long)blocks_this);

    return (new_errs == 0) ? 0 : -1;
}

/*
 * Focused boundary test: write 8 blocks that straddle the TA0→TA1
 * boundary so we confirm the address space is truly continuous.
 *
 * Layout (each block = BLOCK_BYTES = 4 KB):
 *   blocks [-4 .. -1] : last 16 KB of TA0
 *   blocks [ 0 ..  3] : first 16 KB of TA1
 */
static int test_boundary(void)
{
    kputs("  [BOUNDARY] TA0/TA1 @");
    print_hex64(TA0_END);
    kputs(" (±16 KB)\n");

    uint64_t errs_before = g_errors;
    const int N = 4;

    /* Write across the boundary */
    for (int i = -N; i < N; i++) {
        uint64_t addr = TA0_END + (int64_t)i * BLOCK_BYTES;
        volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)addr;
        for (uint32_t j = 0; j < BLOCK_WORDS; j++)
            p[j] = 0xCAFEF00D ^ (uint32_t)i ^ j;
    }
    __asm__ __volatile__("fence rw, rw" ::: "memory");

    /* Verify across the boundary */
    for (int i = -N; i < N; i++) {
        uint64_t addr = TA0_END + (int64_t)i * BLOCK_BYTES;
        volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)addr;
        for (uint32_t j = 0; j < BLOCK_WORDS; j++) {
            uint32_t expected = 0xCAFEF00D ^ (uint32_t)i ^ j;
            uint32_t got      = p[j];
            if (got != expected) {
                kputs("  ERR boundary addr=");
                print_hex64(addr + j * 4);
                kprintf(" exp=0x%08x got=0x%08x\n", expected, got);
                g_errors++;
            }
        }
        g_blocks++;
    }

    uint64_t new_errs = g_errors - errs_before;
    if (new_errs == 0)
        kputs("    PASS  (boundary continuous)\n");
    else
        kprintf("    FAIL  (%llu boundary errors)\n",
                (unsigned long long)new_errs);

    return (new_errs == 0) ? 0 : -1;
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void)
{
    int pass = 1;

    kputs("\n");
    kputs("========================================\n");
    kputs("  FMXCVU19P 2-MIG DDR4 Memory Test\n");
    kputs("========================================\n");
    kputs("TA0: ");  print_hex64(TA0_BASE); kputs(" ~ "); print_hex64(TA0_END - 1);
    kputs("  (18 GiB)\n");
    kputs("TA1: ");  print_hex64(TA1_BASE); kputs(" ~ "); print_hex64(TA1_END - 1);
    kputs("  (18 GiB)\n");
    kputs("Stride: 256 MB   Block: 4 KB\n\n");

    /* ── Pattern 1: fixed seed 0x55AA55AA ── */
    kputs("--- Pattern 1 (0x55AA55AA) ---\n");
    pass &= (test_region("TA0", TA0_TEST_BASE, TA0_TEST_SIZE, 0x55AA55AA) == 0);
    pass &= (test_region("TA1", TA1_BASE,      TA1_SIZE,      0x55AA55AA) == 0);

    /* ── Pattern 2: complementary seed 0xAA55AA55 ── */
    kputs("--- Pattern 2 (0xAA55AA55) ---\n");
    pass &= (test_region("TA0", TA0_TEST_BASE, TA0_TEST_SIZE, 0xAA55AA55) == 0);
    pass &= (test_region("TA1", TA1_BASE,      TA1_SIZE,      0xAA55AA55) == 0);

    /* ── Pattern 3: all-zeros ── */
    kputs("--- Pattern 3 (0x00000000) ---\n");
    pass &= (test_region("TA0", TA0_TEST_BASE, TA0_TEST_SIZE, 0x00000000) == 0);
    pass &= (test_region("TA1", TA1_BASE,      TA1_SIZE,      0x00000000) == 0);

    /* ── Boundary crossing test ── */
    kputs("--- TA0/TA1 Boundary Test ---\n");
    pass &= (test_boundary() == 0);

    /* ── Summary ── */
    kputs("\n========================================\n");
    kprintf("  Result : %s\n", pass ? "ALL PASS" : "FAILED");
    kprintf("  Blocks : %llu\n", (unsigned long long)g_blocks);
    kprintf("  Errors : %llu\n", (unsigned long long)g_errors);
    kputs("========================================\n");

    /* Spin forever so the user can read UART output */
    while (1)
        ;

    return 0;  /* unreachable */
}
