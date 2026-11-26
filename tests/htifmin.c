/* Probe: can we print over the UART (no HTIF) on a WorldGuard config?
   The UART's divisor resets to the value the harness adapter expects, so the
   program only has to enable TX. */
#include <stdint.h>
#include <stdio.h>

#define UART_BASE 0x10020000UL
#define UART_TXDATA (UART_BASE + 0x00)
#define UART_TXCTRL (UART_BASE + 0x08)
#define UART_DIV    (UART_BASE + 0x18)

#define rd32(a)   (*(volatile uint32_t *)(a))
#define wr32(a,v) (*(volatile uint32_t *)(a) = (uint32_t)(v))
#define wr64(a,v) (*(volatile uint64_t *)(a) = (v))

static void uart_init(void) { wr32(UART_TXCTRL, 1u); }
static void uart_putc(char c) {
    while (rd32(UART_TXDATA) & (1u << 31)) { }
    wr32(UART_TXDATA, (uint8_t)c);
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_hex(uint64_t v) {
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) uart_putc("0123456789abcdef"[(v >> i) & 0xf]);
}

static const unsigned long WGC_BASES[] = { 0x06000000UL, 0x02060000UL };
#define WGC_N (sizeof(WGC_BASES)/sizeof(WGC_BASES[0]))
#define SLOT(b,i) ((b) + 0x20 + 0x20*((i)+1))

int main(void)
{
    uart_init();
    uart_puts("UART alive, div="); uart_hex(rd32(UART_DIV)); uart_puts("\n");

    for (unsigned k = 0; k < WGC_N; k++) {
        unsigned long b = WGC_BASES[k];
        wr32(SLOT(b,1) + 0x10, 0);
        wr64(SLOT(b,1) + 0x00, (0x80000000UL | ((0x200000UL >> 1) - 1)) >> 6);
        wr64(SLOT(b,1) + 0x08, ~0ULL);
        wr32(SLOT(b,1) + 0x10, 3u | (1u<<8) | (1u<<9));
        uart_puts("checker "); uart_hex(b);
        uart_puts(" addr="); uart_hex(*(volatile uint64_t *)(SLOT(b,1)+0x00));
        uart_puts(" perm="); uart_hex(*(volatile uint64_t *)(SLOT(b,1)+0x08));
        uart_puts(" cfg=");  uart_hex(rd32(SLOT(b,1)+0x10));
        uart_puts("\n");
    }
    asm volatile ("fence" ::: "memory");

    uart_puts("about to printf\n");
    printf("HTIFMIN printf ok\n");
    uart_puts("printf returned\n");
    return 0;
}
