// See LICENSE for license details.
// Subsystem attach + config fragment for VelaAxiEthRtl (plan 3, no BD).
package velaEth

import chisel3._
import org.chipsalliance.cde.config.{Config, Field, Parameters}
import freechips.rocketchip.subsystem.{BaseSubsystem, FBUS, PBUS}
import freechips.rocketchip.diplomacy.{LazyModule, InModuleBody}
import freechips.rocketchip.tilelink.{TLFragmenter, TLWidthWidget}

case object VelaAxiEthRtlKey extends Field[Option[VelaAxiEthParams]](None)

/** Mix into DigitalTop to optionally add the no-BD AXI-Ethernet + AXI-DMA NIC.
  * DMA's 3 masters -> FBUS (merged by the FBUS xbar); regs <- PBUS; IRQs -> PLIC. */
trait CanHavePeripheryVelaAxiEthRtl { this: BaseSubsystem =>
  val velaAxiEthRtlSgmiiOpt = p(VelaAxiEthRtlKey).map { params =>
    val fbus = locateTLBusWrapper(FBUS)
    val pbus = locateTLBusWrapper(PBUS)
    val domain = fbus.generateSynchronousDomain.suggestName("vela_axi_eth_rtl_domain")
    val vela = domain {
      val v = LazyModule(new VelaAxiEthRtl(params))
      // Three DMA masters merged by the FBUS crossbar (no Xilinx interconnect).
      fbus.coupleFrom("vela_rtl_mm2s") { _ := v.mm2sTL }
      fbus.coupleFrom("vela_rtl_s2mm") { _ := v.s2mmTL }
      fbus.coupleFrom("vela_rtl_sg")   { _ := v.sgTL }
      // Register slaves (holdFirstDeny: control path ends in AXI4 which can DECERR)
      pbus.coupleTo("vela_rtl_dma_ctrl") {
        v.dmaCtrlTLNode := TLFragmenter(4, pbus.blockBytes, holdFirstDeny = true) := TLWidthWidget(pbus.beatBytes) := _
      }
      pbus.coupleTo("vela_rtl_eth_ctrl") {
        v.ethCtrlTLNode := TLFragmenter(4, pbus.blockBytes, holdFirstDeny = true) := TLWidthWidget(pbus.beatBytes) := _
      }
      // Interrupts -> PLIC. Order matters: it fixes the PLIC line numbering,
      // so dma@ gets [mm2s, s2mm] and eth@ gets [mac] in that order.
      ibus.fromSync := v.int_node
      ibus.fromSync := v.mac_int_node
      // Describe the AXI4-Lite control clock (== this FBUS synchronous domain's
      // clock, i.e. the peripheral's axi_aclk / s_axi_lite_clk) on the ethernet
      // node. xilinx_axienet derives the MDIO clock divisor from it; with no
      // `clocks` property it falls back to /cpus/cpu@0 `clock-frequency`, which
      // Chipyard emits as 0, and MDIO registration dies with -EOVERFLOW (-75).
      // Deliberately NOT bound on the DMA node: xilinx_dma requires a named
      // "s_axi_lite_aclk" and currently fails to probe, which is what keeps it
      // from fighting axienet over the same AXI DMA registers (axienet drives
      // them itself via axistream-connected, not through dmaengine).
      fbus.dtsClk.foreach(_.bind(v.ethDevice))
      InModuleBody {
        val io = IO(new VelaEthRtlSgmiiIO).suggestName("vela_eth_rtl_sgmii_inner")
        io <> v.module.sgmii
        io
      }
    }
    InModuleBody {
      val port = IO(new VelaEthRtlSgmiiIO).suggestName("vela_eth_rtl_sgmii")
      port <> vela
      port
    }
  }
}

/** Config fragment: instantiate the plan-3 peripheral. */
class WithVelaAxiEthRtl(params: VelaAxiEthParams = VelaAxiEthParams()) extends Config((site, here, up) => {
  case VelaAxiEthRtlKey => Some(params)
})
