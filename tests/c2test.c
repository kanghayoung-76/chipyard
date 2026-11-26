// C2 reproduction: does a request queued behind another on the same L2 set
// reach the checker under the *previous* request's WID?
//
// Defect (reverted fix), Scheduler.scala:
//     m.io.allocate.bits.wid := m.io.status.bits.wid   // defect
// `viewAsSupertype` already copies wid from the request being loaded, so this
// line overwrites it with the WID of the request the MSHR just finished.
// It fires when `queue` holds (a request whose set matches a busy MSHR) and
// that MSHR later reloads (`will_reload`).
//
// Two harts are needed: one Rocket has a single outstanding cached miss, so it
// cannot put two A-channel requests on one L2 set at the same time.
//
// L2 is 1024 sets x 8 ways x 64 B, so addr[15:6] is the set index and the
// same-set stride is 64 KiB. Region T is 1 MiB and admits WID e alone:
//
//   hart0  WID e   T + k*64KiB, k = 0..7   allowed    keeps L2 set 0 busy
//   hart1  WID x   T + 8*64KiB             refused    queues behind it
//
// Both pick offset 0 inside their block, so every address lands on L2 set 0.
// Each hart's eight addresses also share one L1 set (addr[11:6] = 0) in a
// 4-way L1, so they evict each other and every access reaches the L2.

#include <stdint.h>

/* ---- output (UART; HTIF syscalls do not work on these configs) ---------- */
#define UART_BASE   0x10020000UL
#define UART_TXDATA (UART_BASE + 0x00)
#define UART_TXCTRL (UART_BASE + 0x08)
#define rd32(a)   (*(volatile uint32_t *)(a))
#define rd64(a)   (*(volatile uint64_t *)(a))
#define wr32(a,v) (*(volatile uint32_t *)(a) = (uint32_t)(v))
#define wr64(a,v) (*(volatile uint64_t *)(a) = (v))


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
#define PROG_BASE 0x80000000UL
#define PROG_SIZE 0x200000UL            /* 2 MiB: program, both stacks, flags */
#define T_BASE    0x80200000UL
#define T_SIZE    0x100000UL            /* 1 MiB, NAPOT */
#define L2_STRIDE 0x10000UL             /* 1024 sets * 64 B */
#define H0_WAYS   8
#define H1_ADDR   (T_BASE + 8 * L2_STRIDE)   /* page 0x80280 */

#define WID_E 1                          /* hart0, owns T */
#define WID_X 0                          /* hart1, refused */

#define ITERS 1500
#define PATTERN 0xc2c2a5a5dead0000ULL

/* ---- checker ------------------------------------------------------------- */
static const unsigned long WGC_BASES[] = { 0x06000000UL, 0x02060000UL };
#define WGC_N (sizeof(WGC_BASES)/sizeof(WGC_BASES[0]))
#define WGC_SLOT(b,i) ((b) + 0x20 + 0x20*((i)+1))
#define WGC_SLOT0_CFG(b) ((b) + 0x20 + 0x10)
#define CFG_NAPOT_ERR (3u | (1u<<8) | (1u<<9))

static uint64_t napot(uint64_t base, uint64_t size) { return (base | ((size >> 1) - 1)) >> 6; }
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

static void pmp_open_everything(void)   /* per-hart CSRs: both harts call this */
{
    asm volatile ("csrw pmpaddr0, %0" :: "r"(~0UL >> 2));
    asm volatile ("csrw pmpcfg0, %0" :: "r"((uint64_t)((1UL << 3) | 7UL)));
    asm volatile ("sfence.vma" ::: "memory");
}

static inline void set_mwid(uint64_t w) { asm volatile ("csrw 0x391, %0" :: "r"(w)); asm volatile ("fence" ::: "memory"); }
static inline uint64_t hartid(void) { uint64_t h; asm volatile ("csrr %0, mhartid" : "=r"(h)); return h; }

/* ---- shared state (in PROG, admitted for every WID) --------------------- */
volatile uint64_t go_flag;
volatile uint64_t h1_done;
volatile uint64_t trap_count[2], trap_cause[2], trap_tval[2];
volatile uint64_t ok_count[2], last_val[2];
uint64_t m_resume;
extern void trap_entry(void);

uint64_t handle_trap(uint64_t cause, uint64_t epc, uint64_t tval)
{
    uint64_t h = hartid();
    trap_count[h]++; trap_cause[h] = cause; trap_tval[h] = tval;
    return epc + 4;
}

/* The crt's _start_secondary calls __main, not main; its default (weak)
   definition is a wfi park loop, so hart1 never reached main at all.
   mt-hello.c uses the same override. */
void __main(void);
int main(void) { __main(); return 0; }

void __main(void)
{
    uint64_t h = hartid();

    if (h == 0) {
        uart_init();
        asm volatile ("csrw mtvec, %0" :: "r"((uint64_t)trap_entry));
        pmp_open_everything();
        wgc_program(1, PROG_BASE, PROG_SIZE, ~0ULL);
        for (unsigned k = 0; k < WGC_N; k++) wr32(WGC_SLOT0_CFG(WGC_BASES[k]), (1u<<8) | (1u<<9));
        wgc_program(0, T_BASE, T_SIZE, perm_rw(WID_E));

        /* Terse on purpose: one UART character costs ~43k simulated cycles
           (divisor 4340 per bit). The map is fixed by the source. */
        up("C2\n");

        /* Seed hart0's lines so its accesses return something recognisable. */
        set_mwid(WID_E);
        uart_putc('a');                  /* hart0 mwid set */
        for (unsigned k = 0; k < H0_WAYS; k++) wr64(T_BASE + k * L2_STRIDE, PATTERN | k);
        asm volatile ("fence" ::: "memory");
        uart_putc('b');                  /* hart0 seeded T */

        go_flag = 1;
        asm volatile ("fence" ::: "memory");

        /* hart0 keeps L2 set 0 busy under WID e. */
        for (unsigned i = 0; i < ITERS; i++) {
            for (unsigned k = 0; k < H0_WAYS; k++) {
                uint64_t before = trap_count[0];
                uint64_t v = rd64(T_BASE + k * L2_STRIDE);
                if (trap_count[0] == before) { ok_count[0]++; last_val[0] = v; }
            }
        }

        uart_putc('c');                  /* hart0 loop done */
        while (!h1_done) { }
        asm volatile ("fence" ::: "memory");
        uart_putc('d');                  /* hart1 reported done */

        /* h0: attempts ok traps cause ; h1: attempts ok traps cause lastval */
        up("0,"); ud((uint64_t)ITERS * H0_WAYS); up(","); ud(ok_count[0]);
        up(","); ud(trap_count[0]); up(","); ud(trap_cause[0]); up("\n");
        up("1,"); ud(ITERS); up(","); ud(ok_count[1]);
        up(","); ud(trap_count[1]); up(","); ud(trap_cause[1]);
        up(","); uh(last_val[1] & 0xffffULL); up("\n");
        up("D\n");
        /* Writing tohost here ended the simulation right after the first
           UART character, so the run stops by wall-clock timeout instead. */
        for (;;) { }
    } else {
        asm volatile ("csrw mtvec, %0" :: "r"((uint64_t)trap_entry));
        pmp_open_everything();
        while (!go_flag) { }
        asm volatile ("fence" ::: "memory");
        set_mwid(WID_X);

        for (unsigned i = 0; i < ITERS; i++) {
            uint64_t before = trap_count[1];
            uint64_t v = rd64(H1_ADDR);
            if (trap_count[1] == before) { ok_count[1]++; last_val[1] = v; }
        }

        set_mwid(WID_E);
        h1_done = 1;
        asm volatile ("fence" ::: "memory");
        for (;;) { }
    }
    return;
}
