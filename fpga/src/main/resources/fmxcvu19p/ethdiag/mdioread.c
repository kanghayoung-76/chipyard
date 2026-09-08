/**
 * mdioread.c - read the AXI 1G/2.5G Ethernet Subsystem's MDIO registers from
 *              Linux userspace via /dev/mem, for boards with no ethtool/devmem.
 *
 * The FMXCVU19P design leaves the *external* PHY's MDIO unbonded, but the
 * AXI-Ethernet core's *internal* SGMII PCS/PMA sits on the core's own MDIO bus
 * at PHYADDR=1 (matches `ethernet-pcs@1` in the generated .dts). Its registers
 * are the only window onto what the PHY reports over SGMII in-band status.
 *
 * Register offsets from the kernel this board boots:
 *   software/firemarshal/boards/default/linux-6.8-vela/
 *       drivers/net/ethernet/xilinx/xilinx_axienet.h:166-169
 *
 * Usage:
 *   ./mdioread                 # decode PCS status (the usual case)
 *   ./mdioread <reg>           # raw read of one register at PHYAD 1
 *   ./mdioread <phyad> <reg>   # raw read at an explicit PHY address
 *
 * Requires root. The axienet driver polls this same bus, so each read is
 * repeated and the majority value reported.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

/* AXI-Ethernet control window; see reg = <0x0 0x64200000 0x0 0x40000> in the .dts */
#define AXIENET_BASE   0x64200000UL
#define MAP_SIZE       0x1000UL

#define XAE_MDIO_MC    0x500   /* MII management config  */
#define XAE_MDIO_MCR   0x504   /* MII management control */
#define XAE_MDIO_MRD   0x50C   /* MII management read data */

#define MC_MDIOEN      0x00000040U
#define MCR_PHYAD_SH   24
#define MCR_REGAD_SH   16
#define MCR_OP_READ    0x00008000U
#define MCR_INITIATE   0x00000800U
#define MCR_READY      0x00000080U

#define PCS_PHYAD      1       /* == CONFIG.PHYADDR on axi_ethernet:8.0 */
#define READ_TRIES     5

static volatile uint32_t *regs;

static uint32_t rd32(unsigned off) { return regs[off / 4]; }
static void     wr32(unsigned off, uint32_t v) { regs[off / 4] = v; }

/* Wait for MCR.READY. READY is HIGH when the bus is idle and LOW while a
 * transaction is in flight, so the driver waits both before and after. */
static int wait_ready(void)
{
    int spin;

    for (spin = 0; spin < 1000000; spin++)
        if (rd32(XAE_MDIO_MCR) & MCR_READY)
            return 0;
    return -1;
}

/* One MDIO read cycle, mirroring axienet_mdio_read() in
 * drivers/net/ethernet/xilinx/xilinx_axienet_mdio.c.
 *
 * Two things the driver does that are easy to miss:
 *   - it waits for READY *before* writing MCR (READY is the idle indicator, so
 *     polling only afterwards returns immediately with a stale MRD);
 *   - it clears MC.MDIOEN after every transaction, so MDIO is normally
 *     DISABLED when we arrive and must be re-enabled or INITIATE is a no-op.
 *
 * Returns 16-bit data, or -1 on timeout.
 */
static int mdio_read_once(unsigned phyad, unsigned regad)
{
    uint32_t save_mc = rd32(XAE_MDIO_MC);
    uint32_t div = save_mc & 0x3F;
    int spin, val = -1;

    if (div == 0) {
        div = 19;   /* 100 MHz AXI / (2.5 MHz * 2) - 1; MDC must be <= 2.5 MHz */
        fprintf(stderr, "warning: MC divisor is 0; assuming %u for a 100 MHz "
                        "AXI clock\n", div);
    }

    wr32(XAE_MDIO_MC, div | MC_MDIOEN);

    if (wait_ready() < 0)
        goto out;

    wr32(XAE_MDIO_MCR, ((phyad & 0x1F) << MCR_PHYAD_SH) |
                       ((regad & 0x1F) << MCR_REGAD_SH) |
                       MCR_OP_READ | MCR_INITIATE);

    /* Catch the busy edge so we cannot sample the pre-transaction idle READY.
     * If the transaction is too quick to observe, fall back to a sleep
     * comfortably longer than one MDIO frame (~26 us at 2.5 MHz). */
    for (spin = 0; spin < 10000; spin++)
        if (!(rd32(XAE_MDIO_MCR) & MCR_READY))
            break;
    if (spin == 10000)
        usleep(1000);

    if (wait_ready() < 0)
        goto out;

    val = (int)(rd32(XAE_MDIO_MRD) & 0xFFFF);

out:
    wr32(XAE_MDIO_MC, save_mc);   /* leave MC as the driver expects */
    return val;
}

/* Read READ_TRIES times, return the most frequent value (the driver races us). */
static int mdio_read(unsigned phyad, unsigned regad)
{
    int v[READ_TRIES], i, j, best = -1, bestn = 0;

    for (i = 0; i < READ_TRIES; i++) {
        v[i] = mdio_read_once(phyad, regad);
        if (v[i] < 0)
            return -1;
    }
    for (i = 0; i < READ_TRIES; i++) {
        int n = 0;
        for (j = 0; j < READ_TRIES; j++)
            if (v[j] == v[i])
                n++;
        if (n > bestn) { bestn = n; best = v[i]; }
    }
    return best;
}

static const char *speed_str(unsigned bits)
{
    switch (bits) {
    case 0: return "10 Mbps";
    case 1: return "100 Mbps";
    case 2: return "1000 Mbps";
    default: return "reserved";
    }
}

static void decode(void)
{
    int bmcr, bmsr, lpa;

    bmcr = mdio_read(PCS_PHYAD, 0);
    /* Status bit 2 latches low; read twice for the live value. */
    (void)mdio_read(PCS_PHYAD, 1);
    bmsr = mdio_read(PCS_PHYAD, 1);
    lpa  = mdio_read(PCS_PHYAD, 5);

    if (bmcr < 0 || bmsr < 0 || lpa < 0) {
        fprintf(stderr, "MDIO read timed out - is the MAC out of reset?\n");
        exit(1);
    }

    if ((bmcr == bmsr && bmsr == lpa) || (bmcr & 0x0001)) {
        fprintf(stderr,
                "MDIO reads look bogus (reg0=0x%04x reg1=0x%04x reg5=0x%04x).\n"
                "Distinct registers returning one value, or BMCR bit 0 set "
                "(reserved), means the\nread handshake failed and MRD is stale "
                "- do not interpret these numbers.\n", bmcr, bmsr, lpa);
        exit(1);
    }

    printf("PCS @ MDIO addr %d (internal SGMII PCS/PMA)\n\n", PCS_PHYAD);
    printf("  reg0 BMCR = 0x%04x   AN %s, AN restart %d, loopback %d\n",
           bmcr, (bmcr & 0x1000) ? "enabled" : "DISABLED",
           !!(bmcr & 0x0200), !!(bmcr & 0x4000));
    printf("  reg1 BMSR = 0x%04x   link %d, AN complete %d, remote fault %d\n",
           bmsr, !!(bmsr & 0x0004), !!(bmsr & 0x0020), !!(bmsr & 0x0010));
    printf("  reg5 LPA  = 0x%04x   <- what the external PHY reports over SGMII\n",
           lpa);
    printf("         link      : %d\n", !!(lpa & 0x8000));
    printf("         ack       : %d\n", !!(lpa & 0x4000));
    printf("         duplex    : %s\n", (lpa & 0x1000) ? "full" : "half");
    printf("         speed     : %s\n", speed_str((lpa >> 10) & 3));

    printf("\n=> ");
    if (!(bmsr & 0x0020))
        printf("SGMII auto-negotiation has NOT completed.\n");
    else if (!(lpa & 0x8000))
        printf("SGMII AN done, but the PHY reports NO COPPER LINK.\n"
               "   The 1000BASE-T link is failing between the PHY and the host:\n"
               "   check the cable (all 4 pairs), the magnetics, and the PHY straps.\n");
    else
        printf("PHY reports copper link up at %s %s.\n",
               speed_str((lpa >> 10) & 3),
               (lpa & 0x1000) ? "full-duplex" : "half-duplex");
}

int main(int argc, char **argv)
{
    unsigned phyad = PCS_PHYAD, regad = 0;
    int raw = 0, fd, val;
    void *map;

    if (argc == 2) { raw = 1; regad = strtoul(argv[1], NULL, 0); }
    else if (argc == 3) {
        raw = 1;
        phyad = strtoul(argv[1], NULL, 0);
        regad = strtoul(argv[2], NULL, 0);
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [[phyad] regad]\n", argv[0]);
        return 2;
    }

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "open /dev/mem: %s (run as root)\n", strerror(errno));
        return 1;
    }
    map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
               (off_t)AXIENET_BASE);
    if (map == MAP_FAILED) {
        fprintf(stderr, "mmap 0x%lx: %s\n", AXIENET_BASE, strerror(errno));
        fprintf(stderr, "(a kernel with CONFIG_STRICT_DEVMEM will refuse this)\n");
        close(fd);
        return 1;
    }
    regs = (volatile uint32_t *)map;

    if (!(rd32(XAE_MDIO_MC) & MC_MDIOEN)) {
        fprintf(stderr, "warning: MDIO is disabled (MC=0x%08x). The axienet "
                        "driver normally enables it at probe;\n"
                        "         an empty /sys/bus/mdio_bus/devices/ means "
                        "probe failed - check dmesg.\n",
                rd32(XAE_MDIO_MC));
    }

    if (raw) {
        val = mdio_read(phyad, regad);
        if (val < 0) { fprintf(stderr, "MDIO read timed out\n"); return 1; }
        printf("phy 0x%02x reg %2u = 0x%04x\n", phyad, regad, val);
    } else {
        decode();
    }

    munmap(map, MAP_SIZE);
    close(fd);
    return 0;
}
