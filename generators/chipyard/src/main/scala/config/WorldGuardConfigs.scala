package chipyard

import org.chipsalliance.cde.config.{Config}

class RocketWithWGMConfig extends Config(
  new worldguard.WithWorldGuard(nWorlds = 4, nSlots = 4) ++
  new worldguard.WithNBigRocketCoresWithWGM(1) ++
  new chipyard.config.AbstractConfig)

class DualRocketWithWGMConfig extends Config(
  new worldguard.WithWorldGuard(nWorlds = 4, nSlots = 4) ++
  new worldguard.WithNBigRocketCoresWithWGM(2) ++
  new chipyard.config.AbstractConfig)

class WGRocketConfig extends Config(
  new worldguard.WithWorldGuard(nWorlds = 4, nSlots = 4) ++
  new worldguard.WithWGRocketNBigCores(1) ++
  new freechips.rocketchip.subsystem.WithoutTLMonitors ++
  new chipyard.config.AbstractConfig)

// Phase 2: 8-world variant. Identical to WGRocketConfig except nWorlds=8
// (widWidth=3). Matches the SM firmware built with NWORLDS=8 / OS_WID=6.
class WGRocket8Config extends Config(
  new worldguard.WithWorldGuard(nWorlds = 8, nSlots = 8) ++
  new worldguard.WithWGRocketNBigCores(1) ++
  new freechips.rocketchip.subsystem.WithoutTLMonitors ++
  new chipyard.config.AbstractConfig)

// 16 worlds (widWidth=4). Mid-point between the verified 8-world build and the
// 32-world ceiling; also bisects the cross-enclave SHM data-visibility issue
// seen at 32 worlds (works at widWidth=3, broken at widWidth=5).
class WGRocket16Config extends Config(
  new worldguard.WithWorldGuard(nWorlds = 16, nSlots = 16) ++
  new worldguard.WithWGRocketNBigCores(1) ++
  new freechips.rocketchip.subsystem.WithoutTLMonitors ++
  new chipyard.config.AbstractConfig)

// Architectural maximum: 32 worlds (widWidth=5). perm is 2 bits/world, so
// 32*2 = 64 bits exactly fills the perm register; 33+ would overflow.
// The L2 cross-WID eviction guard is on from here down: before evicting a line
// that belongs to another WID, the L2 asks the memory-port checker whether the
// requester may touch the address, and refuses without evicting when it may
// not. Permitted sharers take the path they always took.
class WGRocket32Config extends Config(
  new worldguard.WithWGL2EvictGuard ++
  new worldguard.WithWorldGuard(nWorlds = 32, nSlots = 32) ++
  new worldguard.WithWGRocketNBigCores(1) ++
  new freechips.rocketchip.subsystem.WithoutTLMonitors ++
  new chipyard.config.AbstractConfig)

// Four-hart variant of WGRocket32Config, for measuring cross-hart paths.
// Only the core count differs: nWorlds/nSlots and every checker stay as they
// are (checkers are per-resource, not per-hart, so hart count does not add
// any). No DMA -- WGDMAKey defaults to None, so CanHaveWGDMA builds nothing.
class WGRocket32x4Config extends Config(
  new worldguard.WithWGL2EvictGuard ++
  new worldguard.WithWorldGuard(nWorlds = 32, nSlots = 32) ++
  new worldguard.WithWGRocketNBigCores(4) ++
  new freechips.rocketchip.subsystem.WithoutTLMonitors ++
  new chipyard.config.AbstractConfig)

// Simulation counterpart of WGRocket32DMAVCU118Config: same core/WG settings
// plus the WID-marked DMA, without the VCU118 harness. Used to exercise the
// four device-side WorldGuard cases before committing to a bitstream.
class WGRocket32DMAConfig extends Config(
  new worldguard.examples.WithWGDMA(wid = 1) ++
  new chipyard.WGRocket32Config)

class WGRocketAndRocketWithWGM extends Config(
  new worldguard.WithWorldGuard(nWorlds = 4, nSlots = 4) ++
  new worldguard.WithOneWGAwareRocketOneRocketWithWGMarker++
  //new worldguard.WithOneWGAwareRocketThreeRocketWithWGMarker ++
  new chipyard.config.AbstractConfig)
