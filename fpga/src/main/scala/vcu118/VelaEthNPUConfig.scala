package chipyard.fpga.vcu118

import org.chipsalliance.cde.config.{Config}

/**
 *  Rocket Core + GemminiNPU (with Pipeline in PE) + Ethernet
 *  default 1 Mig
**/
class VelaRocketHugeGemminiVCU118 extends Config(
  new FPGAGemminiConfig ++                              // FPGA-optimized GemminiNPU with increased pipeline latency
  new WithVelaTestHarness ++                            // Use VelaFPGATestHarness (physical pins only, NIC internal)
  new WithVCU118RJ45Tweaks ++                           // Use RJ45 tweaks instead of regular tweaks
  new WithEthernetPins ++                               // HarnessBinder for Ethernet/SGMII pins
  new chipyard.iobinders.WithXilinxEthAdapter ++        // Override WithNICIOPunchthrough - connects IceNIC to XilinxEth SGMII/RJ45
  new freechips.rocketchip.subsystem.WithInclusiveCache(nWays=8, capacityKB=512) ++
  new freechips.rocketchip.subsystem.WithCoherentBusTopology ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
  new chipyard.config.AbstractConfig)
