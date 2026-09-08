// See LICENSE for license details.
//
// Subsystem attach + config fragment for the Xilinx AXI-Ethernet + AXI-DMA
// peripheral (see VelaAxiEth.scala).
package velaEth

import chisel3._
import org.chipsalliance.cde.config.{Config, Field, Parameters}
import freechips.rocketchip.subsystem.{BaseSubsystem, FBUS, PBUS}
import freechips.rocketchip.diplomacy.LazyModule
import freechips.rocketchip.tilelink.{TLFragmenter, TLWidthWidget}
import freechips.rocketchip.diplomacy.InModuleBody

case object VelaAxiEthKey extends Field[Option[VelaAxiEthParams]](None)

/** Mix into DigitalTop to optionally instantiate the AXI-Ethernet + AXI-DMA NIC.
  *
  *  - DMA memory master  -> FBUS  (coherent DMA into DRAM )
  *  - DMA + MAC registers -> PBUS (MMIO control)
  *  - mm2s/s2mm/mac interrupts -> PLIC (ibus)
  *  - SGMII pins surfaced on the subsystem module for an IOBinder to punch out.
  */
trait CanHavePeripheryVelaAxiEth { this: BaseSubsystem =>
  val velaAxiEthSgmiiOpt = p(VelaAxiEthKey).map { params =>
    val fbus = locateTLBusWrapper(FBUS)
    val pbus = locateTLBusWrapper(PBUS)
    // Peripheral lives in the FBUS clock domain (the bus it masters); do all
    // diplomatic couplings inside the domain so crossings are handled there.
    val domain = fbus.generateSynchronousDomain.suggestName("vela_axi_eth_domain")
    // Punch SGMII out of the peripheral one hierarchy level at a time
    val innerSgmii = domain {
      val v = LazyModule(new VelaAxiEth(params))
      // DMA memory master -> FBUS
      fbus.coupleFrom("vela_axi_dma") { _ := v.dma_tl_node }
      // Register slaves <- PBUS (narrow pbus beat to the 32-bit AXI-Lite slaves)
      // holdFirstDeny=true: the control path ends in AXI4 (via TLToAXI4), which
      // can return DECERR/denials that the fragmenter must hold across beats.
      pbus.coupleTo("vela_dma_ctrl") {
        v.dmaCtrlTLNode := TLFragmenter(4, pbus.blockBytes, holdFirstDeny = true) := TLWidthWidget(pbus.beatBytes) := _
      }
      pbus.coupleTo("vela_eth_ctrl") {
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
      // Lift SGMII from the peripheral module to the domain module boundary.
      InModuleBody {
        val io = IO(new VelaEthSgmiiIO).suggestName("vela_eth_sgmii_inner")
        io <> v.module.sgmii
        io
      }
    }

    // Lift SGMII from the domain module to the subsystem module boundary.
    InModuleBody {
      val port = IO(new VelaEthSgmiiIO).suggestName("vela_eth_sgmii")
      port <> innerSgmii
      port
    }
  }
}

/** Config fragment: instantiate the peripheral with default (or given) params. */
class WithVelaAxiEth(params: VelaAxiEthParams = VelaAxiEthParams()) extends Config((site, here, up) => {
  case VelaAxiEthKey => Some(params)
})
