/* One character per step, so a hang points at the exact line it happened on.
   At ~43k simulated cycles per UART character this run is a few dozen seconds,
   against the C2/C3 tests that showed no progress in 48 minutes. */
#include <stdint.h>

#define UART_BASE   0x10020000UL
#define UART_TXDATA (UART_BASE + 0x00)
#define UART_TXCTRL (UART_BASE + 0x08)
#define rd32(a)   (*(volatile uint32_t *)(a))
#define rd64(a)   (*(volatile uint64_t *)(a))
#define wr32(a,v) (*(volatile uint32_t *)(a) = (uint32_t)(v))
#define wr64(a,v) (*(volatile uint64_t *)(a) = (v))

static void uart_putc(char c) {
    while (rd32(UART_TXDATA) & (1u << 31)) { }
    wr32(UART_TXDATA, (uint8_t)c);
}

#define PROG_BASE 0x80000000UL
#define PROG_SIZE 0x200000UL
#define T_BASE    0x80200000UL
#define T_SIZE    0x10000UL
#define WID_E 1
#define WID_X 0

static const unsigned long WGC_BASES[] = { 0x06000000UL, 0x02060000UL };
#define WGC_N (sizeof(WGC_BASES)/sizeof(WGC_BASES[0]))
#define WGC_SLOT(b,i) ((b) + 0x20 + 0x20*((i)+1))
#define WGC_SLOT0_CFG(b) ((b) + 0x20 + 0x10)
#define CFG_NAPOT_ERR (3u | (1u<<8) | (1u<<9))
static uint64_t napot(uint64_t b, uint64_t s) { return (b | ((s >> 1) - 1)) >> 6; }
static uint64_t perm_rw(int w) { return (1ULL << (w*2)) | (1ULL << (w*2+1)); }
static void wgc_program(int slot, uint64_t base, uint64_t size, uint64_t perm) {
    for (unsigned k = 0; k < WGC_N; k++) {
        unsigned long b = WGC_BASES[k];
        wr32(WGC_SLOT(b,slot) + 0x10, 0);
        wr64(WGC_SLOT(b,slot) + 0x00, napot(base, size));
        wr64(WGC_SLOT(b,slot) + 0x08, perm);
        wr32(WGC_SLOT(b,slot) + 0x10, CFG_NAPOT_ERR);
    }
    asm volatile ("fence" ::: "memory");
}
static inline void set_mwid(uint64_t w) { asm volatile ("csrw 0x391, %0" :: "r"(w)); asm volatile ("fence" ::: "memory"); }

volatile uint64_t trap_count, trap_cause;
uint64_t m_resume;
extern void trap_entry(void);
uint64_t handle_trap(uint64_t cause, uint64_t epc, uint64_t tval)
{ (void)tval; trap_count++; trap_cause = cause; return epc + 4; }

int main(void)
{
    wr32(UART_TXCTRL, 1u);
    uart_putc('A');                                   /* UART up */

    asm volatile ("csrw mtvec, %0" :: "r"((uint64_t)trap_entry));
    asm volatile ("csrw pmpaddr0, %0" :: "r"(~0UL >> 2));
    asm volatile ("csrw pmpcfg0, %0" :: "r"((uint64_t)((1UL << 3) | 7UL)));
    asm volatile ("sfence.vma" ::: "memory");
    uart_putc('B');                                   /* PMP open */

    wgc_program(1, PROG_BASE, PROG_SIZE, ~0ULL);
    for (unsigned k = 0; k < WGC_N; k++) wr32(WGC_SLOT0_CFG(WGC_BASES[k]), (1u<<8) | (1u<<9));
    wgc_program(0, T_BASE, T_SIZE, perm_rw(WID_E));
    uart_putc('C');                                   /* slots programmed */

    set_mwid(WID_E);
    uart_putc('D');                                   /* UART still reachable as WID e */

    volatile uint64_t x = rd64(PROG_BASE + 0x1000);   /* own code region, WID e */
    (void)x;
    uart_putc('E');                                   /* DRAM read inside slot 1 */

    wr64(T_BASE, 0x1234ULL);                          /* T, granted to WID e */
    uart_putc('F');                                   /* write to T */

    volatile uint64_t y = rd64(T_BASE);
    uart_putc(y == 0x1234ULL ? 'G' : 'g');            /* read back from T */

    /* Fill all four ways of L1 set 0 with WID-e lines, clean, exactly as the
       C3 test does -- that is the only thing it has that this probe lacked. */
    for (unsigned w = 0; w < 4; w++) wr64(T_BASE + w * 0x1000UL, 0x5500ULL + w);
    asm volatile ("fence" ::: "memory");
    {   /* push them out, then read them back so they are clean, not dirty */
        volatile uint64_t sink = 0;
        for (uint64_t a = 0x80100000UL; a < 0x80100000UL + (256*1024UL); a += 64) sink += rd64(a);
        (void)sink;
    }
    for (unsigned w = 0; w < 4; w++) (void)rd64(T_BASE + w * 0x1000UL);
    asm volatile ("fence" ::: "memory");
    uart_putc('H');                                   /* L1 set 0 full of WID-e lines */

    /* Four refused loads, one character each: the C3 test stops somewhere in
       this loop and no WGCOBS_DENY is ever logged. */
    for (unsigned i = 0; i < 4; i++) {
        set_mwid(WID_X);
        volatile uint64_t z = rd64(T_BASE + 0x4000UL + i * 64UL);
        (void)z;
        set_mwid(WID_E);
        uart_putc((char)('I' + i));
    }

    uart_putc((char)('0' + (trap_count % 10)));       /* traps seen */
    uart_putc((char)('0' + (trap_cause % 10)));       /* last cause */
    uart_putc('\n');

    /* No HTIF write here: this run only asks how far the program gets. */
    for (;;) { }
    return 0;
}
