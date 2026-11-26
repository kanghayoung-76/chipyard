// C3 reproduction: does a checker-refused grant's payload land in the L1 data
// array on top of an unrelated valid line?
//
// Defect (reverted fix), DCache.scala:
//     dataArb.io.in(1).bits.write := true.B          // defect
//     dataArb.io.in(1).bits.write := !d.bits.denied  // fix
// The paired tag write, metaArb.io.in(3).valid, is gated on !denied in stock
// rocket-chip already, so under the defect the data of refill_way is replaced
// while its tag and coherence state are left alone.
//
// Geometry: L1 D$ is 64 sets x 4 ways x 64 B, so the way stride is 4 KiB and
// the set index is addr[11:6]. Region T is admitted for WID e alone:
//
//   T + 0x0000 + 64*s   way 0 of L1 set s   WID e, pattern
//   T + 0x1000 + 64*s   way 1
//   T + 0x2000 + 64*s   way 2
//   T + 0x3000 + 64*s   way 3
//   T + 0x4000 + 64*s   the address WID x loads and is refused -- same L1 set,
//                       fifth distinct tag, so the refill targets one of the
//                       four ways above
//
// Output is over the UART: HTIF syscalls do not work on these WorldGuard
// configs (first syscall returns "bad syscall #0"), and the UART needs none.

#include <stdint.h>

/* ---- output ------------------------------------------------------------- */
#define UART_BASE   0x10020000UL
#define UART_TXDATA (UART_BASE + 0x00)
#define UART_TXCTRL (UART_BASE + 0x08)
#define rd32(a)     (*(volatile uint32_t *)(a))
#define rd64(a)     (*(volatile uint64_t *)(a))
#define wr32(a,v)   (*(volatile uint32_t *)(a) = (uint32_t)(v))
#define wr64(a,v)   (*(volatile uint64_t *)(a) = (v))


static void uart_init(void) { wr32(UART_TXCTRL, 1u); }
static void uart_putc(char c) {
    while (rd32(UART_TXDATA) & (1u << 31)) { }
    wr32(UART_TXDATA, (uint8_t)c);
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
    while (v) { b[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) uart_putc(b[--n]);
}

/* ---- map ---------------------------------------------------------------- */
#define PROG_BASE  0x80000000UL
#define PROG_SIZE  0x200000UL           /* 2 MiB: program, stack, sweep buffer */
#define SWEEP_BASE 0x80100000UL
#define SWEEP_SIZE (256 * 1024UL)       /* >> 16 KiB L1 D */

#define T_BASE     0x80200000UL
#define T_SIZE     0x10000UL            /* 64 KiB, NAPOT */
#define WAY_STRIDE 0x1000UL             /* 64 sets * 64 B */
#define D_OFF      0x4000UL             /* the refused address's page */
#define NWAYS      4
#define NSETS      8                    /* L1 sets 0..7 under test */
#define LINE       64UL

#define WID_E 1                         /* owns T */
#define WID_X 0                         /* the refused one */

#define PAT_BASE 0xc3c3000000000000ULL
static uint64_t pat(unsigned w, unsigned s) { return PAT_BASE | ((uint64_t)w << 8) | s; }
static uint64_t line_addr(unsigned w, unsigned s) { return T_BASE + w * WAY_STRIDE + s * LINE; }
static uint64_t deny_addr(unsigned s) { return T_BASE + D_OFF + s * LINE; }

/* ---- checker ------------------------------------------------------------- */
static const unsigned long WGC_BASES[] = { 0x06000000UL, 0x02060000UL };
#define WGC_N (sizeof(WGC_BASES)/sizeof(WGC_BASES[0]))
#define WGC_SLOT(b,i) ((b) + 0x20 + 0x20*((i)+1))
#define WGC_SLOT0_CFG(b) ((b) + 0x20 + 0x10)
#define WGC_LGALIGN 6
#define CFG_NAPOT_ERR (3u | (1u<<8) | (1u<<9))

static uint64_t napot(uint64_t base, uint64_t size) { return (base | ((size >> 1) - 1)) >> WGC_LGALIGN; }
static uint64_t perm_rw(int wid) { return (1ULL << (wid*2)) | (1ULL << (wid*2 + 1)); }

static void wgc_program(int slot, uint64_t base, uint64_t size, uint64_t perm)
{
    for (unsigned k = 0; k < WGC_N; k++) {
        unsigned long b = WGC_BASES[k];
        wr32(WGC_SLOT(b,slot) + 0x10, 0);
        wr64(WGC_SLOT(b,slot) + 0x00, napot(base, size));
        wr64(WGC_SLOT(b,slot) + 0x08, perm);
        wr32(WGC_SLOT(b,slot) + 0x10, CFG_NAPOT_ERR);
    }
    asm volatile ("fence" ::: "memory");
}

static void pmp_open_everything(void)
{
    asm volatile ("csrw pmpaddr0, %0" :: "r"(~0UL >> 2));
    asm volatile ("csrw pmpcfg0, %0" :: "r"((uint64_t)((1UL << 3) | 7UL)));
    asm volatile ("sfence.vma" ::: "memory");
}

static inline void set_mwid(uint64_t w) { asm volatile ("csrw 0x391, %0" :: "r"(w)); asm volatile ("fence" ::: "memory"); }

/* ---- traps -------------------------------------------------------------- */
static volatile uint64_t trap_count, trap_cause, trap_tval;
uint64_t m_resume;
extern void trap_entry(void);

uint64_t handle_trap(uint64_t cause, uint64_t epc, uint64_t tval)
{
    trap_count++; trap_cause = cause; trap_tval = tval;
    return epc + 4;   /* step over the faulting load */
}

static void sweep_l1(void)
{
    volatile uint64_t sink = 0;
    for (uint64_t a = SWEEP_BASE; a < SWEEP_BASE + SWEEP_SIZE; a += LINE) sink += rd64(a);
    (void)sink;
    asm volatile ("fence" ::: "memory");
}

int main(void)
{
    uart_init();
    asm volatile ("csrw mtvec, %0" :: "r"((uint64_t)trap_entry));
    pmp_open_everything();
    wgc_program(1, PROG_BASE, PROG_SIZE, ~0ULL);
    for (unsigned k = 0; k < WGC_N; k++) wr32(WGC_SLOT0_CFG(WGC_BASES[k]), (1u<<8) | (1u<<9));
    wgc_program(0, T_BASE, T_SIZE, perm_rw(WID_E));

    /* Output is deliberately terse: the UART divisor is 4340 cycles per bit,
       so one character costs ~43k simulated cycles. Everything the banner used
       to say is fixed by the source and the run command. */
    up("C3\n");

    /* 1. WID e writes its pattern into all four ways of each set under test. */
    set_mwid(WID_E);
    uart_putc('a');                      /* mwid set */
    for (unsigned w = 0; w < NWAYS; w++)
        for (unsigned s = 0; s < NSETS; s++)
            wr64(line_addr(w, s), pat(w, s));
    asm volatile ("fence" ::: "memory");
    uart_putc('b');                      /* pattern written */

    /* 2. Push them out and read them back, so they sit clean in L1: a dirty
          victim would block the refused Acquire (acquireBeforeRelease = false
          means tl_out_a.valid needs !s2_victim_dirty). */
    sweep_l1();
    uart_putc('c');                      /* L1 swept */
    for (unsigned w = 0; w < NWAYS; w++)
        for (unsigned s = 0; s < NSETS; s++)
            (void)rd64(line_addr(w, s));
    asm volatile ("fence" ::: "memory");
    uart_putc('d');                      /* reloaded clean */

    /* 3. WID x loads the fifth tag of each set and is refused. */
    trap_count = 0;
    for (unsigned s = 0; s < NSETS; s++) {
        set_mwid(WID_X);
        uint64_t v;
        asm volatile ("ld %0, 0(%1)" : "=r"(v) : "r"(deny_addr(s)) : "memory");
        set_mwid(WID_E);
        (void)v;
    }
    uart_putc('e');                      /* refused loads done */
    up("t"); ud(trap_count); up(" c"); ud(trap_cause); up("\n");

    /* 4. WID e reads its own lines back and compares. */
    set_mwid(WID_E);
    unsigned bad = 0;
    for (unsigned w = 0; w < NWAYS; w++) {
        for (unsigned s = 0; s < NSETS; s++) {
            uint64_t got = rd64(line_addr(w, s));
            uint64_t want = pat(w, s);
            if (got != want) {
                bad++;
                /* way, set, and the low 16 bits of what came back; the full
                   expected value is pat(w,s) = 0xc3c3_0000_0000_<w><s>. */
                up("M"); ud(w); up(","); ud(s); up(",");
                uh(got & 0xffffULL); up(got == 0 ? ",Z\n" : ",O\n");
            }
        }
    }
    up("m"); ud(bad); up("/"); ud(NWAYS * NSETS); up("\nD\n");
    /* Writing tohost here ended the simulation right after the first UART
       character, so the run stops by wall-clock timeout instead. */
    for (;;) { }
    return 0;
}
