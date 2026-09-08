package chipyard.iobinders

import chisel3._

import velaEth.CanHavePeripheryVelaAxiEth

// EthernetIOBundle / EthernetPort are defined in chipyard.iobinders.Ports.
// This binder punches the AXI-Ethernet peripheral's SGMII pins up to ChipTop
// as an EthernetPort, so the existing VCU118 `WithEthernetPins` HarnessBinder
// connects them to the RJ45 MGT exactly as it did for the IceNIC/XilinxEth path.

/**
 * WithVelaAxiEthAdapter - surface the Xilinx AXI-Ethernet SGMII pins at ChipTop.
 *
 * Unlike WithXilinxEthAdapter (which bridged IceNIC <-> a harness blackbox),
 * the MAC + DMA now live inside DigitalTop as a diplomatic peripheral, so this
 * binder only forwards physical pins — no packet/stream logic here.
 */
class WithVelaAxiEthAdapter extends OverrideIOBinder({
  (system: CanHavePeripheryVelaAxiEth) => {
    system.velaAxiEthSgmiiOpt.map { sgmii =>
      val ethIO = IO(new EthernetIOBundle).suggestName("ethernet")

      ethIO.sgmii_rxp.suggestName("ethernet_sgmii_rxp")
      ethIO.sgmii_rxn.suggestName("ethernet_sgmii_rxn")
      ethIO.sgmii_txp.suggestName("ethernet_sgmii_txp")
      ethIO.sgmii_txn.suggestName("ethernet_sgmii_txn")

      // Map the BD peripheral's LVDS-SGMII pins onto the existing EthernetIOBundle.
      // lvds_clk (125 MHz) reuses the eth_gt_refclk pins (same board LOCs).
      // ChipTop drives the peripheral's inputs; peripheral drives ChipTop outputs.
      sgmii.lvds_clk_clk_p := ethIO.eth_gt_refclk_p
      sgmii.lvds_clk_clk_n := ethIO.eth_gt_refclk_n
      sgmii.sgmii_rxp      := ethIO.sgmii_rxp.asBool
      sgmii.sgmii_rxn      := ethIO.sgmii_rxn.asBool

      ethIO.sgmii_txp := sgmii.sgmii_txp.asUInt
      ethIO.sgmii_txn := sgmii.sgmii_txn.asUInt
      // BD does not expose link/speed status pins; park the LEDs.
      ethIO.link_up         := 0.U
      ethIO.speed_is_100    := 0.U
      ethIO.speed_is_10_100 := 0.U

      println("[VelaAxiEth] WithVelaAxiEthAdapter: SGMII pins punched to ChipTop")
      (Seq(EthernetPort(() => ethIO)), Nil)
    }.getOrElse((Nil, Nil))
  }
})
