package velaEth

import chisel3._
import chisel3.util._


class XilinxEthBlackBox extends BlackBox with HasBlackBoxResource {
  val io = IO(new Bundle {
    // FPGA Pin signals
    // System Reset (no sys_clk needed - IP generates clocks from gt_refclk)
    val sys_rst         = Input(Bool())

    // GT Reference Clock (IP generates userclk2/rxuserclk2 internally)
    val gt_refclk_p     = Input(Clock())
    val gt_refclk_n     = Input(Clock())

    // SGMII
    val sgmii_txp       = Output(UInt(1.W))
    val sgmii_txn       = Output(UInt(1.W))
    val sgmii_rxp       = Input(UInt(1.W))
    val sgmii_rxn       = Input(UInt(1.W))

    // Link Status 
    val link_up         = Output(UInt(1.W)) // For LED Indicator
    val speed_is_100    = Output(UInt(1.W)) // For LED Indicator
    val speed_is_10_100 = Output(UInt(1.W)) // For LED Indicator

    // FPGA internal signals
    // AXIS signals
    val s_axis_tx_valid = Input(Bool())
    val s_axis_tx_data  = Input(UInt(64.W))
    val s_axis_tx_keep  = Input(UInt(8.W))
    val s_axis_tx_last  = Input(Bool())
    val s_axis_tx_ready = Output(Bool())

    val m_axis_rx_valid = Output(Bool())
    val m_axis_rx_data  = Output(UInt(64.W))
    val m_axis_rx_keep  = Output(UInt(8.W))
    val m_axis_rx_last  = Output(Bool())
    val m_axis_rx_ready = Input(Bool())

  })

  println("[USER]XilinxBlackBox Included ")
  addResource("vsrc/XilinxEth_SGMII_Wrapper.sv")
}
