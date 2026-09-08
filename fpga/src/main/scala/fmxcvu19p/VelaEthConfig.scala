package chipyard.fpga.fmxcvu19p

import sys.process._

import org.chipsalliance.cde.config.{Config, Parameters}
import freechips.rocketchip.subsystem.{SystemBusKey, PeripheryBusKey, ControlBusKey, ExtMem, FBUS, PBUS}
import freechips.rocketchip.devices.debug.{DebugModuleKey, ExportDebug, JTAG}
import freechips.rocketchip.devices.tilelink.{DevNullParams, BootROMLocated}
import freechips.rocketchip.diplomacy.{RegionType, AddressSet}
import freechips.rocketchip.resources.{DTSModel, DTSTimebase}

import sifive.blocks.devices.spi.{PeripherySPIKey, SPIParams}
import sifive.blocks.devices.uart.{PeripheryUARTKey, UARTParams}

import sifive.fpgashells.shell.{DesignKey}
import sifive.fpgashells.shell.xilinx.{FMXCVU19PShellPMOD, FMXCVU19PDDRSize}

import testchipip.serdes.{SerialTLKey}
import velaVPU.common.{VectorParams}
import rerocc._


import chipyard._
import chipyard.harness._
import chipyard.iobinders._
import icenet.{NICAttachKey}


// Custom FMXCVU19P tweaks without WithETHPassthrough (handled internally by OMNI)
class WithEthernet extends Config(
  new icenet.WithIceNIC ++
  new Config((site, here, up) => {
    case NICAttachKey => icenet.NICAttachParams(
      masterWhere = FBUS,
      slaveWhere = PBUS
    )
  })
)

class WithFMXCVU19PRJ45Tweaks extends Config(
  // clocking
  new chipyard.harness.WithAllClocksFromHarnessClockInstantiator ++
  new chipyard.clocking.WithPassthroughClockGenerator ++
  new chipyard.config.WithUniformBusFrequencies(100) ++
  new WithFPGAFrequency(100) ++ // 90MHz: reduced from 100MHz to close timing on Gemmini paths
  // harness binders
  new WithUART ++
  new WithSPISDCard ++
  new WithDDRMem ++
  new WithEthernet ++ // for IceNic
  new WithJTAG ++
  // other configuration
  //new WithDefaultPeripherals ++
  new WithFMCSDPeripherals ++
  new chipyard.config.WithSPI(BigInt(0x64001000L)) ++ // add SPI controller
  new chipyard.config.WithTLBackingMemory ++ // use TL backing memory
  new WithSystemModifications ++ // setup busses, use sdboot bootrom, setup ext. mem. size
  new chipyard.config.WithNoDebug ++ // remove debug module
  new freechips.rocketchip.subsystem.WithoutTLMonitors ++
  new freechips.rocketchip.subsystem.WithNMemoryChannels(1) // 1 TL channel → TLXbar in TestHarness routes by address to TA0/TA1
)

class RocketHugePoolGemmini extends Config(
  new gemmini.DefaultGemminiConfig ++
  new WithNumofMemory ++
  new WithVelaTestHarness ++                  // Use VelaFPGATestHarness (physical pins only, NIC internal)
  new WithFMXCVU19PRJ45Tweaks ++              // Use RJ45 tweaks instead of regular tweaks
  new WithEthernetPins ++
  new freechips.rocketchip.subsystem.WithInclusiveCache(nWays=8, capacityKB=512) ++
  new freechips.rocketchip.subsystem.WithCoherentBusTopology ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++                                // single rocket-core
  new chipyard.config.AbstractConfig)

/**
 *  Boom Core + Gemmini + Saturn
 *  default 2 Mig
**/
class VelaBoomMediumGemminiSaturn extends Config(
  new gemmini.DefaultGemminiConfig ++
  new velaVPU.rocket.WithRocketVectorUnit(512, 256, VectorParams.refParams) ++
  new chipyard.config.WithSystemBusWidth(256) ++
  new WithVelaTestHarness ++                  // Use VelaFPGATestHarness (physical pins only, NIC internal)
  new WithFMXCVU19PRJ45Tweaks ++              // Use RJ45 tweaks instead of regular tweaks
  new WithEthernetPins ++
  new freechips.rocketchip.subsystem.WithInclusiveCache(nWays=8, capacityKB=512) ++
  new freechips.rocketchip.subsystem.WithCoherentBusTopology ++
  new boom.v3.common.WithNMediumBooms(1) ++     
  new chipyard.config.AbstractConfig)
