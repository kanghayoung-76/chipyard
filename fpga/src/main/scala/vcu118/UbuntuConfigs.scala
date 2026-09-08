package chipyard.fpga.vcu118

import chisel3._

import org.chipsalliance.cde.config.Config
import org.chipsalliance.diplomacy.lazymodule._
import freechips.rocketchip.diplomacy.{Resource, ResourceBinding, ResourceAddress}

import sifive.blocks.devices.spi.{HasPeripherySPI, MMCDevice}

import chipyard.iobinders.{OverrideLazyIOBinder, SPIPort}

// Isolated variant of chipyard.iobinders.WithSPIIOPunchthrough that exposes the
// SD card's SPI clock instead of hardcoding 1 MHz. Kept separate from the shared
// IOBinders.scala so the buildroot/nodisk RocketVCU118Config flow is untouched.
class WithMMCSPIMaxMHz(mhz: Double) extends OverrideLazyIOBinder({
  (system: HasPeripherySPI) => {
    // attach resource to 1st SPI
    if (system.tlSpiNodes.size > 0) ResourceBinding {
      Resource(new MMCDevice(system.tlSpiNodes.head.device, mhz), "reg").bind(ResourceAddress(0))
    }
    InModuleBody {
      val spi = system.spi
      val ports = spi.zipWithIndex.map({ case (s, i) =>
        val io_spi = IO(s.cloneType).suggestName(s"spi_$i")
        io_spi <> s
        SPIPort(() => io_spi)
      })
      (ports, Nil)
    }
  }
})

// Ubuntu (disk-based rootfs on the SPI SD card) variant of RocketVCU118Config.
// Composes on top of the existing, working WithVCU118Tweaks rather than editing it.
// Start at 1 MHz (matches today's default) for the first hardware smoke test; raise
// once mmc_spi is confirmed probing correctly on real hardware (see docs/Prototyping/VCU118-Ubuntu.rst).
class RocketVCU118UbuntuConfig extends Config(
  new WithMMCSPIMaxMHz(1) ++
  new WithVCU118Tweaks ++
  new chipyard.RocketConfig
)
