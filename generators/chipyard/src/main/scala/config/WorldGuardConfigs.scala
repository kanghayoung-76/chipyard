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
  new worldguard.WithWorldGuard(nWorlds = 16, nSlots = 4) ++
  new worldguard.WithWGRocketNBigCores(1) ++
  new freechips.rocketchip.subsystem.WithoutTLMonitors ++
  new chipyard.config.AbstractConfig)

// Architectural maximum: 32 worlds (widWidth=5). perm is 2 bits/world, so
// 32*2 = 64 bits exactly fills the perm register; 33+ would overflow.
class WGRocket32Config extends Config(
  new worldguard.WithWorldGuard(nWorlds = 32, nSlots = 4) ++
  new worldguard.WithWGRocketNBigCores(1) ++
  new freechips.rocketchip.subsystem.WithoutTLMonitors ++
  new chipyard.config.AbstractConfig)

class WGRocketAndRocketWithWGM extends Config(
  new worldguard.WithWorldGuard(nWorlds = 4, nSlots = 4) ++
  new worldguard.WithOneWGAwareRocketOneRocketWithWGMarker++
  //new worldguard.WithOneWGAwareRocketThreeRocketWithWGMarker ++
  new chipyard.config.AbstractConfig)
