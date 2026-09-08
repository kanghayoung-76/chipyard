package chipyard.fpga.vcu118

import sys.process._

import org.chipsalliance.cde.config.{Config, Parameters}
import freechips.rocketchip.subsystem.{SystemBusKey, PeripheryBusKey, ControlBusKey, ExtMem}
import freechips.rocketchip.devices.debug.{DebugModuleKey, ExportDebug, JTAG}
import freechips.rocketchip.devices.tilelink.{DevNullParams, BootROMLocated}
import freechips.rocketchip.diplomacy.{RegionType, AddressSet}
import freechips.rocketchip.resources.{DTSModel, DTSTimebase}

import sifive.blocks.devices.spi.{PeripherySPIKey, SPIParams}
import sifive.blocks.devices.uart.{PeripheryUARTKey, UARTParams}

import sifive.fpgashells.shell.{DesignKey}
import sifive.fpgashells.shell.xilinx.{VCU118ShellPMOD, VCU118ShellSDLocation, VCU118DDRSize}

import testchipip.serdes.{SerialTLKey}
import testchipip.soc.{BankedScratchpadKey}

import chipyard._
import chipyard.harness._

class WithDefaultPeripherals extends Config((site, here, up) => {
  case PeripheryUARTKey => List(UARTParams(address = BigInt(0x64000000L)))
  case PeripherySPIKey => List(SPIParams(rAddress = BigInt(0x64001000L)))
  case VCU118ShellPMOD => "SDIO"
})

// Use FMC HPC1 (J2) connector for SD card via TB-FMCL-PH breakout board
// Better signal integrity for higher SPI clock speeds (up to 25MHz)
class WithFMCSDPeripherals extends Config((site, here, up) => {
  case PeripheryUARTKey => List(UARTParams(address = BigInt(0x64000000L)))
  case PeripherySPIKey => List(SPIParams(rAddress = BigInt(0x64001000L)))
  case VCU118ShellSDLocation => "FMC"  // SD card on FMC instead of PMOD
  case VCU118ShellPMOD => "JTAG"       // PMOD can be used for JTAG when SD is on FMC
})

class WithSystemModifications extends Config((site, here, up) => {
  case DTSTimebase => BigInt((1e6).toLong)
  case BootROMLocated(x) => up(BootROMLocated(x), site).map { p =>
    // invoke makefile for sdboot
    val freqMHz = (site(SystemBusKey).dtsFrequency.get / (1000 * 1000)).toLong
    val make = s"make -C fpga/src/main/resources/vcu118/sdboot PBUS_CLK=${freqMHz} bin"
    require (make.! == 0, "Failed to build bootrom")
    p.copy(hang = 0x10000, contentFileName = s"./fpga/src/main/resources/vcu118/sdboot/build/sdboot.bin")
  }
  case ExtMem => up(ExtMem, site).map(x => x.copy(master = x.master.copy(size = site(VCU118DDRSize)))) // set extmem to DDR size
  case SerialTLKey => Nil // remove serialized tl port
})

// Fix scratchpad address conflict with VCU118 DDR memory at 0x80000000
class WithVCU118SafeScratchpad extends Config((site, here, up) => {
  case BankedScratchpadKey => up(BankedScratchpadKey).map { params =>
    params.copy(base = 0x70000000L) // Move scratchpad to safe address
  }
})

// DOC include start: AbstractVCU118 and Rocket
class WithVCU118Tweaks extends Config(
  // clocking
  new chipyard.harness.WithAllClocksFromHarnessClockInstantiator ++
  new chipyard.clocking.WithPassthroughClockGenerator ++
  new chipyard.config.WithUniformBusFrequencies(100) ++
  new WithFPGAFrequency(100) ++ // default 100MHz freq
  // harness binders
  new WithUART ++
  new WithSPISDCard ++
  new WithDDRMem ++
  new WithJTAG ++
  // other configuration
  new WithDefaultPeripherals ++
  new chipyard.config.WithSPI(BigInt(0x64001000L)) ++ // add SPI controller
  new chipyard.config.WithTLBackingMemory ++ // use TL backing memory
  new WithSystemModifications ++ // setup busses, use sdboot bootrom, setup ext. mem. size
  new WithVCU118SafeScratchpad ++ // Fix scratchpad address conflict with DDR
  new freechips.rocketchip.subsystem.WithoutTLMonitors ++
  new freechips.rocketchip.subsystem.WithNMemoryChannels(1)
)

class RocketVCU118Config extends Config(
  new WithVCU118FMCSDTweaks ++
  new chipyard.RocketConfig
)
// DOC include end: AbstractVCU118 and Rocket

// VCU118 with SD card on FMC HPC1 (J2) via TB-FMCL-PH breakout board
// Use this config for better SD card signal integrity (higher SPI clock speeds)
class WithVCU118FMCSDTweaks extends Config(
  // clocking
  new chipyard.harness.WithAllClocksFromHarnessClockInstantiator ++
  new chipyard.clocking.WithPassthroughClockGenerator ++
  new chipyard.config.WithUniformBusFrequencies(100) ++
  new WithFPGAFrequency(100) ++ // default 100MHz freq
  // harness binders
  new WithUART ++
  new WithSPISDCard ++
  new WithDDRMem ++
  new WithJTAG ++
  // other configuration - use FMC for SD card
  new WithFMCSDPeripherals ++
  new chipyard.config.WithSPI(BigInt(0x64001000L)) ++ // add SPI controller
  new chipyard.config.WithTLBackingMemory ++ // use TL backing memory
  new WithSystemModifications ++ // setup busses, use sdboot bootrom, setup ext. mem. size
  new WithVCU118SafeScratchpad ++ // Fix scratchpad address conflict with DDR
  new freechips.rocketchip.subsystem.WithoutTLMonitors ++
  new freechips.rocketchip.subsystem.WithNMemoryChannels(1)
)

// Rocket config with SD card on FMC connector (TB-FMCL-PH)
class RocketVCU118FMCSDConfig extends Config(
  new WithVCU118FMCSDTweaks ++
  new chipyard.RocketConfig
)

// ================= WorldGuard configs (ported from Vyond chipyard-1.13) =================
// 8-world (nWorlds=8, widWidth=3) WorldGuard-aware Rocket, SD card on FMC (TB-FMCL-PH).
// Pinned to 50 MHz: matches the timing point WorldGuard was verified at on Vyond's
// chipyard-1.13/1.11.0 (WithVCU118FMCSDTweaks defaults to 100 MHz).
class WGRocket8VCU118Config extends Config(
  new WithFPGAFreq100MHz ++
  new WithVCU118FMCSDTweaks ++
  new chipyard.WGRocket8Config
)


// DOC RISC-V VELA 
class WithVelaTestHarness extends Config((site, here, up) => {
  case sifive.fpgashells.shell.DesignKey => (p: Parameters) => new VelaFPGATestHarness()(p)
})


class BoomVCU118Config extends Config(
  new WithFPGAFrequency(50) ++
  new WithVCU118Tweaks ++
  new chipyard.MegaBoomV3Config
)

class WithFPGAFrequency(fMHz: Double) extends Config(
  new chipyard.harness.WithHarnessBinderClockFreqMHz(fMHz) ++
  new chipyard.config.WithSystemBusFrequency(fMHz) ++
  new chipyard.config.WithPeripheryBusFrequency(fMHz) ++
  new chipyard.config.WithControlBusFrequency(fMHz) ++
  new chipyard.config.WithFrontBusFrequency(fMHz) ++
  new chipyard.config.WithMemoryBusFrequency(fMHz)
)

class WithFPGAFreq25MHz extends WithFPGAFrequency(25)
class WithFPGAFreq50MHz extends WithFPGAFrequency(50)
class WithFPGAFreq75MHz extends WithFPGAFrequency(75)
class WithFPGAFreq100MHz extends WithFPGAFrequency(100)
