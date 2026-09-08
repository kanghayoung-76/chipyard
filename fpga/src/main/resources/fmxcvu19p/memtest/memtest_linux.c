/**
 * memtest_linux.c - FMXCVU19P 2-MIG DDR4 Continuous Address Range Test
 *                   Linux userspace version using /dev/mem + mmap
 *
 * Usage:
 *   gcc -O2 -o memtest_linux memtest_linux.c
 *   sudo ./memtest_linux          # full test (slow, ~72 blocks)
 *   sudo ./memtest_linux --quick  # boundary only
 *
 * Memory map:
 *   TA0: 0x0_8000_0000 ~ 0x4_7FFF_FFFF  (16 GiB, MIG0 / DDRTA0)
 *   TA1: 0x4_8000_0000 ~ 0x8_7FFF_FFFF  (16 GiB, MIG1 / DDRTA1)
 *   DDR4_AxiAddressWidth=34 → 2^34=16 GiB per MIG (physical 18 GiB, 2 GiB unreachable)
 *
 * Note: requires /dev/mem access (run as root or with CAP_SYS_RAWIO).
 *       Linux kernel must not have CONFIG_STRICT_DEVMEM enabled,
 *       or the physical range must be excluded from the kernel's
 *       memory map (e.g., via mem= boot arg for the test region).
 */

#define _FILE_OFFSET_BITS 64   /* ensure 64-bit off_t on all platforms */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

/* ── Memory map ─────────────────────────────────────────────────────── */
#define TA0_BASE  ((uint64_t)0x80000000ULL)
#define TA0_SIZE  ((uint64_t)0x400000000ULL)   /* 16 GiB (2^34, DDR4_AxiAddressWidth=34) */
#define TA0_END   (TA0_BASE + TA0_SIZE)         /* 0x480000000 */

#define TA1_BASE  TA0_END                        /* 0x480000000 */
#define TA1_SIZE  ((uint64_t)0x400000000ULL)   /* 16 GiB */
#define TA1_END   (TA1_BASE + TA1_SIZE)         /* 0x880000000 */

/*
 * Safe start in TA0: skip the bottom 256 MB to avoid the region
 * where the Linux kernel and page tables are loaded.
 */
#define SAFE_OFFSET    ((uint64_t)0x10000000ULL) /* 256 MB */
#define TA0_TEST_BASE  (TA0_BASE + SAFE_OFFSET)
#define TA0_TEST_SIZE  (TA0_SIZE - SAFE_OFFSET)

/* ── Test parameters ─────────────────────────────────────────────────── */
#define MAP_CHUNK      ((uint64_t)0x1000000ULL)  /* mmap 16 MB at a time  */
#define BLOCK_BYTES    (4096U)                   /* test 4 KB per block   */
#define BLOCK_WORDS    (BLOCK_BYTES / 4)
#define STRIDE         ((uint64_t)0x10000000ULL) /* 256 MB between blocks */
#define MAX_PRINT_ERR  8                         /* errors printed/block  */

/* ── Globals ─────────────────────────────────────────────────────────── */
static int    g_devmem_fd = -1;
static uint64_t g_blocks  = 0;
static uint64_t g_errors  = 0;

/* ── mmap helper ─────────────────────────────────────────────────────── */
static void *map_phys(uint64_t phys, size_t len)
{
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
                   MAP_SHARED, g_devmem_fd, (off_t)phys);
    if (p == MAP_FAILED) {
        fprintf(stderr, "  mmap 0x%llx len=%zu: %s\n",
                (unsigned long long)phys, len, strerror(errno));
        return NULL;
    }
    return p;
}

static void unmap_phys(void *p, size_t len)
{
    munmap(p, len);
}

/* ── Test one 4 KB block ─────────────────────────────────────────────── */
static int test_block(void *virt, uint64_t phys, uint32_t seed)
{
    volatile uint32_t *p = (volatile uint32_t *)virt;
    int errs = 0;

    /* Write */
    for (uint32_t i = 0; i < BLOCK_WORDS; i++)
        p[i] = seed ^ i;

    /* Memory barrier */
    __sync_synchronize();

    /* Read back */
    for (uint32_t i = 0; i < BLOCK_WORDS; i++) {
        uint32_t exp = seed ^ i;
        uint32_t got = p[i];
        if (got != exp) {
            if (errs < MAX_PRINT_ERR)
                printf("  ERR phys=0x%010llx exp=0x%08x got=0x%08x\n",
                       (unsigned long long)(phys + i * 4), exp, got);
            errs++;
            g_errors++;
        }
    }
    return errs;
}

/* ── Test a region at stride intervals ───────────────────────────────── */
static int test_region(const char *tag, uint64_t base, uint64_t size,
                       uint32_t pattern)
{
    printf("  [%s]  0x%010llx ~ 0x%010llx\n",
           tag,
           (unsigned long long)base,
           (unsigned long long)(base + size - 1));

    uint64_t errs_before = g_errors;
    uint64_t blocks_this = 0;
    int      last_done   = 0;
    uint64_t off         = 0;

    while (!last_done) {
        if (off + BLOCK_BYTES > size)
            break;

        uint64_t phys = base + off;

        /* Map a chunk containing this block */
        uint64_t chunk_base = phys & ~(MAP_CHUNK - 1);
        size_t   chunk_len  = (size_t)MAP_CHUNK;
        uint64_t blk_off    = phys - chunk_base;

        void *virt = map_phys(chunk_base, chunk_len);
        if (!virt) {
            printf("    SKIP (mmap failed)\n");
            goto next;
        }

        test_block((char *)virt + blk_off, phys, pattern);
        unmap_phys(virt, chunk_len);
        g_blocks++;
        blocks_this++;

next:
        {
            uint64_t next_off = off + STRIDE;
            if (next_off + BLOCK_BYTES > size) {
                uint64_t last_off = size - BLOCK_BYTES;
                if (last_off != off) {
                    off = last_off;
                    continue;
                }
                last_done = 1;
            } else {
                off = next_off;
            }
        }
    }

    uint64_t new_errs = g_errors - errs_before;
    if (new_errs == 0)
        printf("    PASS  (%llu blocks)\n", (unsigned long long)blocks_this);
    else
        printf("    FAIL  (%llu errors / %llu blocks)\n",
               (unsigned long long)new_errs,
               (unsigned long long)blocks_this);

    return (new_errs == 0) ? 0 : -1;
}

/* ── TA0/TA1 boundary test ───────────────────────────────────────────── */
static int test_boundary(void)
{
    printf("  [BOUNDARY]  0x%010llx (±16 KB across TA0/TA1)\n",
           (unsigned long long)TA0_END);

    /*
     * Map 32 KB centred on the boundary:
     * [TA0_END - 16KB, TA0_END + 16KB)
     * This crosses the two MIG regions in one contiguous mmap window.
     */
    const size_t  window = 32 * 1024;          /* 32 KB total        */
    uint64_t      phys   = TA0_END - 16 * 1024;
    /* Align down to MAP_CHUNK boundary */
    uint64_t      cbase  = phys & ~(MAP_CHUNK - 1);
    size_t        clen   = (size_t)MAP_CHUNK;
    uint64_t      voff   = phys - cbase;

    void *virt = map_phys(cbase, clen);
    if (!virt) {
        printf("    SKIP (mmap failed)\n");
        return -1;
    }

    volatile uint32_t *p = (volatile uint32_t *)((char *)virt + voff);
    uint32_t words = window / 4;
    uint64_t errs_before = g_errors;

    /* Write across the boundary */
    for (uint32_t i = 0; i < words; i++)
        p[i] = 0xCAFEF00D ^ i;

    __sync_synchronize();

    /* Verify across the boundary */
    for (uint32_t i = 0; i < words; i++) {
        uint32_t exp = 0xCAFEF00D ^ i;
        uint32_t got = p[i];
        if (got != exp) {
            uint64_t pa = phys + i * 4;
            const char *which = (pa < TA0_END) ? "TA0" : "TA1";
            printf("  ERR [%s] phys=0x%010llx exp=0x%08x got=0x%08x\n",
                   which, (unsigned long long)pa, exp, got);
            g_errors++;
            if (g_errors - errs_before >= MAX_PRINT_ERR)
                break;
        }
    }
    g_blocks++;

    unmap_phys(virt, clen);

    uint64_t new_errs = g_errors - errs_before;
    if (new_errs == 0)
        printf("    PASS  (boundary continuous, TA0→TA1 seamless)\n");
    else
        printf("    FAIL  (%llu errors at/near boundary)\n",
               (unsigned long long)new_errs);

    return (new_errs == 0) ? 0 : -1;
}

/* ── main ────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    int quick_mode = (argc > 1 && strcmp(argv[1], "--quick") == 0);

    printf("\n");
    printf("========================================\n");
    printf("  FMXCVU19P 2-MIG DDR4 Memory Test\n");
    printf("  (Linux /dev/mem userspace)\n");
    printf("========================================\n");
    printf("TA0: 0x%010llx ~ 0x%010llx  (18 GiB)\n",
           (unsigned long long)TA0_BASE, (unsigned long long)TA0_END - 1);
    printf("TA1: 0x%010llx ~ 0x%010llx  (18 GiB)\n",
           (unsigned long long)TA1_BASE, (unsigned long long)TA1_END - 1);
    printf("Mode: %s   Stride: 256 MB   Block: 4 KB\n\n",
           quick_mode ? "QUICK (boundary only)" : "FULL");

    g_devmem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (g_devmem_fd < 0) {
        perror("open /dev/mem");
        fprintf(stderr, "Hint: run as root or with CAP_SYS_RAWIO\n");
        return 1;
    }

    int pass = 1;

    if (!quick_mode) {
        printf("--- Pattern 1 (0x55AA55AA) ---\n");
        pass &= (test_region("TA0", TA0_TEST_BASE, TA0_TEST_SIZE, 0x55AA55AA) == 0);
        pass &= (test_region("TA1", TA1_BASE,      TA1_SIZE,      0x55AA55AA) == 0);

        printf("--- Pattern 2 (0xAA55AA55) ---\n");
        pass &= (test_region("TA0", TA0_TEST_BASE, TA0_TEST_SIZE, 0xAA55AA55) == 0);
        pass &= (test_region("TA1", TA1_BASE,      TA1_SIZE,      0xAA55AA55) == 0);
    }

    printf("--- TA0/TA1 Boundary Test ---\n");
    pass &= (test_boundary() == 0);

    close(g_devmem_fd);

    printf("\n========================================\n");
    printf("  Result : %s\n", pass ? "ALL PASS" : "FAILED");
    printf("  Blocks : %llu\n", (unsigned long long)g_blocks);
    printf("  Errors : %llu\n", (unsigned long long)g_errors);
    printf("========================================\n\n");

    return pass ? 0 : 1;
}
