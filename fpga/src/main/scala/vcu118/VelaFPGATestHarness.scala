package chipyard.fpga.vcu118 

import chisel3._
import chisel3.experimental.{Analog}
import freechips.rocketchip.diplomacy._
import org.chipsalliance.cde.config._
import freechips.rocketchip.subsystem._
import freechips.rocketchip.tilelink._

import sifive.fpgashells.shell._
import sifive.fpgashells.shell.xilinx._
import sifive.fpgashells.ip.xilinx._
import sifive.fpgashells.clocks._

import sifive.blocks.devices.uart._
import sifive.blocks.devices.spi._
import sifive.blocks.devices.gpio._

import chipyard._
import chipyard.harness._
import chipyard.iobinders._


class VelaFPGATestHarness(override implicit val p: Parameters) extends VCU118FPGATestHarness()(p) {

  val ethIO = 
    Some(InModuleBody {
      val io = IO(new Bundle {
        val eth_gt_refclk_p = Input(Clock())
        val eth_gt_refclk_n = Input(Clock())
        val sgmii_rxp = Input(Bool())
        val sgmii_rxn = Input(Bool())
        val sgmii_txp = Output(Bool())
        val sgmii_txn = Output(Bool())
        val link_up = Output(Bool())
        val speed_is_100 = Output(Bool())
        val speed_is_10_100 = Output(Bool())
      })
      io.suggestName("ethIO")

      // Default drivers for every output sink.
      // ethIO is created unconditionally, but only a config that punches an
      // EthernetPort (WithVelaAxiEthAdapter / WithXilinxEthAdapter) has a
      // WithEthernetPins binder to drive these. Without defaults, an
      // Ethernet-less config fails with "sink ethIO_* not fully initialized".
      // InModuleBody blocks run inside LazyRawModuleImp.instantiate(), i.e.
      // BEFORE the harness Imp body calls instantiateChipTops(), so the
      // HarnessBinder's connections come later and win by last-connect.
      //io.sgmii_txp       := false.B
      //io.sgmii_txn       := false.B
      //io.link_up         := false.B
      //io.speed_is_100    := false.B
      //io.speed_is_10_100 := false.B

      // Add XDC constraints for RJ45/SGMII pins (VCU118 SGMII via Bank 67 LVDS Bitslice)
      // Based on VCU118 Board User Guide (UG1224) and gig_ethernet_pcs_pma IP requirements
      // Reference: VCU118 has SGMII on J52 RJ45 connector using LVDS bitslice transceivers

      // 125MHz SGMII reference clock (typically from onboard oscillator or external source)
      // For VCU118, use SGMII reference clock 2 (MGTREFCLK2) which can be 125MHz
      xdc.addPackagePin(io.eth_gt_refclk_p, "AY24")   // SGMII_REF_CLK_P (Bank 227/228)
      xdc.addPackagePin(io.eth_gt_refclk_n, "AY23")   // SGMII_REF_CLK_P (Bank 227/228)

      // SGMII LVDS pairs - Bank 67 (HP I/O bank for SGMII on VCU118)
      // These are connected to the Ethernet PHY for SGMII interface
      // Note: Actual pins depend on VCU118 board revision - verify with board schematic
      xdc.addPackagePin(io.sgmii_rxp.asBool, "AU24")  // Bank 67 LVDS RX+ (from PHY to FPGA)
      xdc.addPackagePin(io.sgmii_rxn.asBool, "AV24")  // Bank 67 LVDS RX- (from PHY to FPGA)
      xdc.addPackagePin(io.sgmii_txp.asBool, "AU21")  // Bank 67 LVDS TX+ (from FPGA to PHY)
      xdc.addPackagePin(io.sgmii_txn.asBool, "AV21")  // Bank 67 LVDS TX- (from FPGA to PHY)
      // Indicator
      xdc.addPackagePin(io.link_up, "AT32")    // To LED GPIO_LED0
      xdc.addPackagePin(io.speed_is_100, "AY30") // To LED GPIO_LED2
      xdc.addPackagePin(io.speed_is_10_100, "AV34")  // To LED GPIO_LED1

      // Add I/O standards for SGMII pins
      xdc.addIOStandard(io.eth_gt_refclk_p, "LVDS")
      xdc.addIOStandard(io.eth_gt_refclk_n, "LVDS")
      xdc.addIOStandard(io.sgmii_rxp, "LVDS")
      xdc.addIOStandard(io.sgmii_rxn, "LVDS")
      xdc.addIOStandard(io.sgmii_txp, "LVDS")
      xdc.addIOStandard(io.sgmii_txn, "LVDS")

      xdc.addIOStandard(io.link_up, "LVCMOS12")
      xdc.addIOStandard(io.speed_is_10_100, "LVCMOS12")
      xdc.addIOStandard(io.speed_is_100, "LVCMOS12")

      println("[VelaQSFPTestHarness] Ethernet/SGMII IO ENABLED")

      io
    })

  override lazy val module = new VelaFPGATestHarnessImp(this)
}

class VelaFPGATestHarnessImp(_outer: VelaFPGATestHarness) extends VCU118FPGATestHarnessImp(_outer) {

  // Provide access to outer harness for harness binder - following VCU118 pattern  
  // No separate pin definitions or connections needed
  println("[DEBUG] VelaFPGATestHarness: Physical pins ready for HarnessBinder connection")

}
