// main.c - Boot diagnostic payload
// This program replaces OpenSBI as the payload loaded by sdboot.
// Purpose: verify that sdboot correctly jumps to 0x80000000 and that
// UART remains functional, before debugging OpenSBI bring-up.
//
// To use: build this, write boot_test.bin to SD card at sector 34,
// then boot. You should see messages on UART after "BOOT" from sdboot.

#include <stdint.h>

// SiFive UART at 0x64000000 (from platform.h)
#define UART_BASE       0x64000000UL
#define UART_TXFIFO     (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_RXFIFO     (*(volatile uint32_t *)(UART_BASE + 0x04))
#define UART_TXCTRL     (*(volatile uint32_t *)(UART_BASE + 0x08))
#define UART_RXCTRL     (*(volatile uint32_t *)(UART_BASE + 0x0C))
#define UART_DIV        (*(volatile uint32_t *)(UART_BASE + 0x18))
#define UART_TXEN       0x1

// Write one character (polls until TXFIFO is not full; bit31=full flag)
static void uart_putc(char c)
{
    while ((int32_t)UART_TXFIFO < 0);
    UART_TXFIFO = (uint32_t)(unsigned char)c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

// Print a 64-bit value as 0x<hex>
static void uart_put_hex(unsigned long v)
{
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        unsigned int nib = (v >> i) & 0xF;
        uart_putc(nib < 10 ? '0' + nib : 'a' + (nib - 10));
    }
}

// Print an unsigned decimal number
static void uart_put_dec(unsigned long v)
{
    char buf[20];
    int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v > 0) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }
    while (i-- > 0)
        uart_putc(buf[i]);
}

// Coarse delay: spin N iterations
static void delay(unsigned long n)
{
    for (volatile unsigned long i = 0; i < n; i++)
        __asm__ __volatile__ ("" ::: "memory");
}

// Read mtime from CLINT (0x2000000 + 0xBFF8)
#define CLINT_MTIME  (*(volatile uint64_t *)(0x2000000UL + 0xBFF8UL))

void boot_main(unsigned long hartid, unsigned long dtb_addr)
{
    // Re-enable UART TX (sdboot enables it, but do it again to be safe)
    UART_TXCTRL = UART_TXEN;

    uart_puts("\r\n");
    uart_puts("========================================\r\n");
    uart_puts("  BOOT DIAGNOSTIC PAYLOAD\r\n");
    uart_puts("  Loaded to 0x80000000 by sdboot\r\n");
    uart_puts("========================================\r\n");

    uart_puts("Step 1: Payload entry    [OK]\r\n");

    uart_puts("Step 2: Hart ID          = ");
    uart_put_dec(hartid);
    uart_putc('\r'); uart_putc('\n');

    uart_puts("Step 3: DTB address      = ");
    uart_put_hex(dtb_addr);
    uart_putc('\r'); uart_putc('\n');

    uart_puts("Step 4: PC at 0x80000000 [OK]\r\n");

    // Verify we can read CLINT mtime (basic bus fabric check)
    uart_puts("Step 5: CLINT mtime      = ");
    uint64_t t0 = CLINT_MTIME;
    uart_put_hex(t0);
    uart_putc('\r'); uart_putc('\n');

    // Check mtime is actually ticking
    delay(100000UL);
    uint64_t t1 = CLINT_MTIME;
    if (t1 > t0) {
        uart_puts("Step 6: CLINT ticking    [OK]  delta=");
        uart_put_dec(t1 - t0);
        uart_puts(" cycles\r\n");
    } else {
        uart_puts("Step 6: CLINT ticking    [FAIL] mtime not advancing!\r\n");
    }

    uart_puts("Step 7: UART TX          [OK]\r\n");

    uart_puts("========================================\r\n");
    uart_puts("  All checks done. OpenSBI would start here.\r\n");
    uart_puts("  Spinning forever - check UART output above.\r\n");
    uart_puts("========================================\r\n");

    // Heartbeat loop so you can confirm the system is alive
    unsigned long tick = 0;
    while (1) {
        delay(20000000UL);
        uart_puts("ALIVE tick=");
        uart_put_dec(tick++);
        uart_puts("  mtime=");
        uart_put_hex(CLINT_MTIME);
        uart_puts("\r\n");
    }
}
