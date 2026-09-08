package chipyard.fpga.fmxcvu19p

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
  case (th: FMXCVU19PFPGATestHarnessImp, port: UARTPort, chipId: Int) => {
    th.fmxcvu19pOuter.io_uart_bb.bundle <> port.io
  }
})

/*** SPI ***/
class WithSPISDCard extends HarnessBinder({
  case (th: FMXCVU19PFPGATestHarnessImp, port: SPIPort, chipId: Int) => {
    th.fmxcvu19pOuter.io_spi_bb.bundle <> port.io
  }
})


/*** DDR (single TL channel → TLXbar → TA0 + TA1 MIGs by address) ***/
class WithDDRMem extends HarnessBinder({
  case (th: FMXCVU19PFPGATestHarnessImp, port: TLMemPort, chipId: Int) => {
    val memPortBag = port.io
    require(memPortBag.size == 1,
      s"WithDDRMem: expected 1 memory channel (WithNMemoryChannels(1)), got ${memPortBag.size}")
    val ddrClient = th.fmxcvu19pOuter.ddrClient
    val bundles = ddrClient.out.map(_._1)
    val ddrClientBundle = Wire(new HeterogeneousBag(bundles.map(_.cloneType)))
    bundles.zip(ddrClientBundle).foreach { case (bundle, io) => bundle <> io }
    ddrClientBundle(0) <> memPortBag(0)
  }
})

class WithJTAG extends HarnessBinder({
  case (th: FMXCVU19PFPGATestHarnessImp, port: JTAGPort, chipId: Int) => {
    val jtag_io = th.fmxcvu19pOuter.jtagPlacedOverlay.overlayOutput.jtag.getWrappedValue
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

      println(s"[DEBUG] HarnessBinder connecting Ethernet pins from ChipTop to TestHarness")
      th match {
        case oth: VelaFPGATestHarnessImp =>
          val harnessOuter = oth.fmxcvu19pOuter.asInstanceOf[VelaFPGATestHarness]
          harnessOuter.ethIO match {
            case Some(ethModuleValue) =>
              val harnessIO = ethModuleValue.getWrappedValue
              val chipIO = port.io
              chipIO.eth_gt_refclk_p    := harnessIO.eth_gt_refclk_p
              chipIO.eth_gt_refclk_n    := harnessIO.eth_gt_refclk_n
              chipIO.sgmii_rxp          := harnessIO.sgmii_rxp
              chipIO.sgmii_rxn          := harnessIO.sgmii_rxn
              harnessIO.sgmii_txp       := chipIO.sgmii_txp
              harnessIO.sgmii_txn       := chipIO.sgmii_txn
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

/* Origin
class WithEthernetPins extends HarnessBinder({
  case (th: HasHarnessInstantiators, port: EthernetPort, chipId: Int) => {
    def tieOffInputs(): Unit = {
      port.io.eth_gt_refclk_p := DontCare
      port.io.eth_gt_refclk_n := DontCare
      port.io.sgmii_rxp := DontCare
      port.io.sgmii_rxn := DontCare
    }

    if(VelaFMXCVU19PBuildConfig.useNPU) {
      println(s"[DEBUG] HarnessBinder connecting Ethernet pins from ChipTop to TestHarness")
      th match {
        case oth: VelaFPGATestHarnessImp =>
          val harnessOuter = oth.fmxcvu19pOuter.asInstanceOf[VelaFPGATestHarness]
          // Use match + explicit getWrappedValue: avoids relying on the moduleValue
          // implicit (not imported here) and guarantees all input sinks are driven.
          harnessOuter.ethIO match {
            case Some(ethModuleValue) =>
              val harnessIO = ethModuleValue.getWrappedValue
              val chipIO = port.io
              chipIO.eth_gt_refclk_p    := harnessIO.eth_gt_refclk_p
              chipIO.eth_gt_refclk_n    := harnessIO.eth_gt_refclk_n
              chipIO.sgmii_rxp          := harnessIO.sgmii_rxp
              chipIO.sgmii_rxn          := harnessIO.sgmii_rxn
              harnessIO.sgmii_txp       := chipIO.sgmii_txp
              harnessIO.sgmii_txn       := chipIO.sgmii_txn
              harnessIO.link_up         := chipIO.link_up
              harnessIO.speed_is_100    := chipIO.speed_is_100
              harnessIO.speed_is_10_100 := chipIO.speed_is_10_100
              println(s"[DEBUG] WithEthernetPins: Ethernet connections established")
            case None =>
              println("[WARNING] WithEthernetPins: ethIO is None despite useNPU=true, tying to DontCare")
              tieOffInputs()
          }
        case _ =>
          println(s"[WARNING] WithEthernetPins: TestHarness is not VelaFPGATestHarnessImp, tying to DontCare")
          tieOffInputs()
      }
    } else {
      println("[DEBUG] WithEthernetPins: Ethernet DISABLED (useNPU=false)")
      tieOffInputs()
    }
  }
})
*/
