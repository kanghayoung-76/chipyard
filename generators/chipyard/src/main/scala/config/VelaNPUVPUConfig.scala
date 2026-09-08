package chipyard

import org.chipsalliance.cde.config.{Config}


// DOC include start: VelaGemminiRocketConfig
class VelaGemminiRocketConfig extends Config(
  new velaNPU.DefaultGemminiConfig ++                            // use GemminiNPU systolic array GEMM accelerator
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
//  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)
// DOC include end: VelaGemminiRocketConfig

// DOC include start: VelaGemminiRocketConfig
class VelaNPUVPURocketConfig extends Config(
  new velaNPU.DefaultGemminiConfig ++                            // use GemminiNPU systolic array GEMM accelerator
  new velaVPU.rocket.WithRocketVectorUnit(512, 256, velaVPU.common.VectorParams.refParams) ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
//  new chipyard.config.WithSystemBusWidth(128) ++
  new chipyard.config.AbstractConfig)
// DOC include end: VelaGemminiRocketConfig
