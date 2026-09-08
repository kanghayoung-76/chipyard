package chipyard.fpga.fmxcvu19p

import org.chipsalliance.cde.config.Config

class ReRoCCGemminiNPUConfig extends Config(
  new rerocc.WithReRoCC ++
  new FPGAGemminiConfig ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
  new chipyard.config.AbstractConfig)
