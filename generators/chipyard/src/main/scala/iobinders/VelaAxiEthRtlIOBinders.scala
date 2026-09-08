package chipyard.iobinders

import chisel3._

import velaEth.CanHavePeripheryVelaAxiEthRtl

/**
 * WithVelaAxiEthRtlAdapter - surface the plan-3 (no-BD) peripheral's LVDS SGMII
 * pins at ChipTop as an EthernetPort, reusing the existing VCU118 WithEthernetPins
 * harness binder (lvds_clk reuses the eth_gt_refclk board pins; LEDs parked).
 */
class WithVelaAxiEthRtlAdapter extends OverrideIOBinder({
  (system: CanHavePeripheryVelaAxiEthRtl) => {
    system.velaAxiEthRtlSgmiiOpt.map { sgmii =>
      val ethIO = IO(new EthernetIOBundle).suggestName("ethernet")
      ethIO.sgmii_rxp.suggestName("ethernet_sgmii_rxp")
      ethIO.sgmii_rxn.suggestName("ethernet_sgmii_rxn")
      ethIO.sgmii_txp.suggestName("ethernet_sgmii_txp")
      ethIO.sgmii_txn.suggestName("ethernet_sgmii_txn")

      sgmii.lvds_clk_clk_p := ethIO.eth_gt_refclk_p
      sgmii.lvds_clk_clk_n := ethIO.eth_gt_refclk_n
      sgmii.sgmii_rxp      := ethIO.sgmii_rxp.asBool
      sgmii.sgmii_rxn      := ethIO.sgmii_rxn.asBool

      ethIO.sgmii_txp := sgmii.sgmii_txp.asUInt
      ethIO.sgmii_txn := sgmii.sgmii_txn.asUInt
      ethIO.link_up         := 0.U
      ethIO.speed_is_100    := 0.U
      ethIO.speed_is_10_100 := 0.U

      println("[VelaAxiEthRtl] WithVelaAxiEthRtlAdapter: SGMII pins punched to ChipTop")
      (Seq(EthernetPort(() => ethIO)), Nil)
    }.getOrElse((Nil, Nil))
  }
})
