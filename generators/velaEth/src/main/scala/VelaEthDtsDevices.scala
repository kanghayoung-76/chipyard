// See LICENSE for license details.
//
// Custom SimpleDevice subclasses that emit the xilinx_axienet driver "glue"
// device-tree properties straight from elaboration -- so the Chipyard-generated
// <config>.dts is already driver-ready and no manual overlay merge is needed.
//
// A stock SimpleDevice.describe() only emits compatible/reg/interrupts. These
// override describe() (the same pattern as rocket-chip's SimpleBus) to append:
//   dma@:       dma-coherent
//   ethernet@:  phy-mode="sgmii", managed="in-band-status", dma-coherent,
//               axistream-connected/-control-connected = <&dma>  (phandle),
//               pcs-handle = <&pcs> + an mdio { ethernet-pcs@N {..} } subnode,
//               xlnx,rxmem.
//
// Phandles use Device.label (rocket-chip labels every device node "L<n>"), and
// ResourceMap(labels=...) puts a resolvable label on the PCS subnode.
package velaEth

import freechips.rocketchip.resources._

/** AXI DMA node + coherency hint (FBUS->L2 coherent DMA). */
class VelaEthDmaDtsDevice
    extends SimpleDevice("dma", Seq("xlnx,axi-dma-1.00.a")) {
  override def describe(resources: ResourceBindings): Description = {
    val Description(name, mapping) = super.describe(resources)
    Description(name, mapping ++ Map[String, Seq[ResourceValue]](
      "dma-coherent" -> Seq()   // empty value -> emits `dma-coherent;`
    ))
  }
}

/** AXI 1G/2.5G Ethernet Subsystem node + the xilinx_axienet SGMII driver glue.
  *
  * Property set is dictated by drivers/net/ethernet/xilinx/xilinx_axienet_main.c
  * (linux 6.8) -- see the notes on each entry below:
  *
  *  - `interrupts` on THIS node is the MAC IRQ. axienet takes the DMA rx/tx IRQs
  *    from the axistream-connected node (index 1 / 0) but its own core IRQ from
  *    `platform_get_irq_optional(pdev, 0)` on the ethernet node. Without it the
  *    probe logs "Ethernet core IRQ not defined" and RX-error/link interrupts
  *    are disabled (XAE_IE is programmed to 0).
  *  - `clocks` must be present. axienet_mdio_enable() derives the MDIO divisor
  *    from clk_get_rate(s_axi_lite_clk), and only falls back to the /cpus/cpu@0
  *    `clock-frequency` when the node has no clocks. Chipyard emits
  *    clock-frequency=<0> for the tile (RocketCoreParams.bootFreqHz defaults to
  *    0), so the fallback computes clk_div = 0/(2*2.5MHz) - 1 = 0xffffffff and
  *    MDIO registration fails with -EOVERFLOW (-75). See VelaAxiEthPeriphery.
  *  - `pcs-handle` + `managed = "in-band-status"`: PHY_TYPE=SGMII makes the IP
  *    instantiate an internal 1G/2.5G PCS/PMA that answers MDIO at the IP's
  *    PHYADDR (Vivado default 1). The external RJ45 PHY's MDIO is NOT wired out
  *    of the block design, so there is no PHY for phylink to attach: declaring
  *    `managed = "in-band-status"` makes phylink take link state from the PCS's
  *    SGMII in-band status instead of requiring a phy-handle (which would fail
  *    open() with -ENODEV). The PCS child carries a non-PHY `compatible` so
  *    of_mdiobus_register_device() registers it verbatim rather than probing it
  *    for a C22 PHY ID (an ID probe that reads back 0xffff would leave the node
  *    device-less and force axienet's probe to -EPROBE_DEFER forever).
  *  - `xlnx,rxmem` mirrors the IP's RXMEM parameter (4k default); axienet needs
  *    it to allow any MTU change (axienet_change_mtu rejects everything when it
  *    is 0).
  *
  * NOTE: no `local-mac-address` -- that property is a 6-BYTE array and
  * rocket-chip's ResourceValue set has no byte-string type (ResourceInt emits
  * 32-bit cells). The driver falls back to eth_hw_addr_random(), so pin the
  * address from userspace (`ip link set dev eth0 address ..`) if a stable MAC
  * is needed.
  *
  * @param dmaDev  the DMA device (for the axistream-connected phandle)
  * @param pcsAddr MDIO address of the IP's internal PCS/PMA (IP PHYADDR param)
  * @param rxmem   RX memory size of the MAC in bytes (IP RXMEM param)
  */
class VelaEthMacDtsDevice(dmaDev: Device, pcsAddr: Int = 1, rxmem: BigInt = 0x1000)
    extends SimpleDevice("ethernet", Seq("xlnx,axi-ethernet-1.00.a")) {
  override def describe(resources: ResourceBindings): Description = {
    val Description(name, mapping) = super.describe(resources)
    val pcsLabel = "vela_eth_pcs"
    val extra: Map[String, Seq[ResourceValue]] = Map(
      "phy-mode"                    -> Seq(ResourceString("sgmii")),
      "managed"                     -> Seq(ResourceString("in-band-status")),
      "dma-coherent"                -> Seq(),
      "axistream-connected"         -> Seq(ResourceReference(dmaDev.label)),
      "axistream-control-connected" -> Seq(ResourceReference(dmaDev.label)),
      "pcs-handle"                  -> Seq(ResourceReference(pcsLabel)),
      "xlnx,rxmem"                  -> Seq(ResourceInt(rxmem)),
      "mdio" -> Seq(ResourceMap(Map(
        "#address-cells" -> Seq(ResourceInt(1)),
        "#size-cells"    -> Seq(ResourceInt(0)),
        s"ethernet-pcs@$pcsAddr" -> Seq(ResourceMap(
          Map("compatible" -> Seq(ResourceString("xlnx,pcs-pma")),
              "reg"        -> Seq(ResourceInt(pcsAddr))),
          labels = Seq(pcsLabel)))
      )))
    )
    Description(name, mapping ++ extra)
  }
}
