package chipyard.fpga.vcu118

import chisel3._
import chisel3.experimental.{BaseModule}

import org.chipsalliance.diplomacy.nodes.{HeterogeneousBag}
import freechips.rocketchip.tilelink.{TLBundle}

import sifive.blocks.devices.uart.{UARTPortIO}
import sifive.blocks.devices.spi.{HasPeripherySPI, SPIPortIO}

import chipyard._
import chipyard.harness._
import chipyard.iobinders._

/*** UART ***/
class WithUART extends HarnessBinder({
  case (th: VCU118FPGATestHarnessImp, port: UARTPort, chipId: Int) => {
    th.vcu118Outer.io_uart_bb.bundle <> port.io
  }
})

/*** SPI ***/
class WithSPISDCard extends HarnessBinder({
  case (th: VCU118FPGATestHarnessImp, port: SPIPort, chipId: Int) => {
    th.vcu118Outer.io_spi_bb.bundle <> port.io
  }
})

/*** Experimental DDR ***/
class WithDDRMem extends HarnessBinder({
  case (th: VCU118FPGATestHarnessImp, port: TLMemPort, chipId: Int) => {
    val bundles = th.vcu118Outer.ddrClient.out.map(_._1)
    val ddrClientBundle = Wire(new HeterogeneousBag(bundles.map(_.cloneType)))
    bundles.zip(ddrClientBundle).foreach { case (bundle, io) => bundle <> io }
    ddrClientBundle <> port.io
  }
})

class WithJTAG extends HarnessBinder({
  case (th: VCU118FPGATestHarnessImp, port: JTAGPort, chipId: Int) => {
    val jtag_io = th.vcu118Outer.jtagPlacedOverlay.overlayOutput.jtag.getWrappedValue
    port.io.TCK := jtag_io.TCK
    port.io.TMS := jtag_io.TMS
    port.io.TDI := jtag_io.TDI
    jtag_io.TDO.data := port.io.TDO
    jtag_io.TDO.driven := true.B
    // ignore srst_n
    jtag_io.srst_n := DontCare

  }
})

/*** Ethernet/SGMII pins for RJ45 (created by WithXilinxEthAdapter) ***/
// This binder connects the Ethernet pins from ChipTop to the TestHarness physical pins

class WithEthernetPins extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: EthernetPort, chipId: Int) => {
    def tieOffInputs(): Unit = {
      port.io.eth_gt_refclk_p := DontCare
      port.io.eth_gt_refclk_n := DontCare
      port.io.sgmii_rxp := DontCare
      port.io.sgmii_rxn := DontCare
    }

    // No VELA_TYPE guard here on purpose. This binder only runs when the config
    // actually punched an EthernetPort, and in that case the harness ethIO pins
    // MUST be connected -- gating on VelaBuildConfig left ethIO_sgmii_tx*,
    // ethIO_link_up and ethIO_speed_* undriven ("sink not fully initialized").
    println(s"[DEBUG] HarnessBinder connecting Ethernet pins from ChipTop to TestHarness")
    th match {
      case oth: VelaFPGATestHarnessImp =>
        val harnessOuter = oth.vcu118Outer.asInstanceOf[VelaFPGATestHarness]
        // ethIO is declared as Some(...) in VelaFPGATestHarness, so foreach
        // always runs; getWrappedValue avoids needing the moduleValue implicit.
        harnessOuter.ethIO.foreach { ethModuleValue =>
            val harnessIO = ethModuleValue.getWrappedValue
            val chipIO = port.io

            // Clock inputs (to ChipTop)
            chipIO.eth_gt_refclk_p    := harnessIO.eth_gt_refclk_p
            chipIO.eth_gt_refclk_n    := harnessIO.eth_gt_refclk_n

            // SGMII RX (inputs to ChipTop from PHY/board)
            chipIO.sgmii_rxp          := harnessIO.sgmii_rxp
            chipIO.sgmii_rxn          := harnessIO.sgmii_rxn

            // SGMII TX (outputs from ChipTop to PHY/board)
            harnessIO.sgmii_txp       := chipIO.sgmii_txp
            harnessIO.sgmii_txn       := chipIO.sgmii_txn

            // Status outputs (from ChipTop to TestHarness LEDs)
            harnessIO.link_up         := chipIO.link_up
            harnessIO.speed_is_100    := chipIO.speed_is_100
            harnessIO.speed_is_10_100 := chipIO.speed_is_10_100

            println(s"[DEBUG] WithEthernetPins: Ethernet connections established")
        }
      case _ =>
        println(s"[WARNING] WithEthernetPins: TestHarness is not VelaFPGATestHarnessImp, tying to DontCare")
        tieOffInputs()
    }
  }
})
