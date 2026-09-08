package chipyard

import org.chipsalliance.cde.config.{Config}


class VelaNormalTest extends Config(
  new chipyard.harness.WithSimEthernetLoopback ++                   // sim TestHarness: drive 125MHz refclk; XilinxEth sim model loops GMII TX->RX
  new chipyard.iobinders.WithXilinxEthAdapter ++                    // route IceNIC through XilinxEth SGMII/RJ45 instead of default IOBinder
  new icenet.WithIceNIC(inBufFlits = 1800) ++                       // enable IceNIC (checksumOffload=true by default in WithIceNIC)
  new gemmini.DefaultGemminiConfig ++                               // use Gemmini systolic array GEMM accelerator
  new freechips.rocketchip.subsystem.WithInclusiveCache(nWays=8, capacityKB=512) ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
  new chipyard.config.AbstractConfig)

