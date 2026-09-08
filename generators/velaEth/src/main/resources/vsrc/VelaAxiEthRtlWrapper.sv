// =============================================================================
// VelaAxiEthRtlWrapper.sv  (PLAN 3, no block design)
//
// Hand-wired wrapper: standalone AXI 1G/2.5G Ethernet Subsystem (vela_rtl_eth)
// + AXI DMA (vela_rtl_dma). The DMA's three memory masters are brought out
// individually (merged in the TileLink fabric, not here).
//
//   DMA m_axis_mm2s (32b) -> ETH s_axis_txd     (TX)
//   ETH m_axis_rxd  (32b) -> DMA s_axis_s2mm    (RX)
//   DMA s_axi_lite  <- s_axi_dma_*  ;  ETH s_axi <- s_axi_eth_*   (AXI-Lite ctrl)
//   DMA m_axi_mm2s / m_axi_s2mm / m_axi_sg -> exposed to Scala (3x AXI4ToTL->FBUS)
//   ETH sgmii + lvds_clk (125 MHz) -> RJ45 pins ; interrupts -> PLIC
//
// LVDS RIU/bitslice-ready inputs are tied off exactly as the block design did
// (riu_*=0, *_dly_rdy=1, *_vtc_rdy=1); MDIO/txc/rxs/signal_detect parked.
//
// !!! IP names (vela_rtl_eth / vela_rtl_dma) + exact port names come from the
//     standalone create_ip in ip_vela_axi_eth_rtl.tcl; reconcile if the IP
//     version changes. Same LVDS config as the plan-1 block design.
// =============================================================================
module VelaAxiEthRtlWrapper #(
    parameter MM_DATA_W = 64,
    parameter MM_ADDR_W = 38,
    parameter SG_DATA_W = 32
) (
    input  wire                   axi_aclk,
    input  wire                   axi_aresetn,

    // ---- M_AXI_MM2S (read-only) ----
    output wire                   m_axi_mm2s_arvalid,
    input  wire                   m_axi_mm2s_arready,
    output wire [MM_ADDR_W-1:0]   m_axi_mm2s_araddr,
    output wire [7:0]             m_axi_mm2s_arlen,
    output wire [2:0]             m_axi_mm2s_arsize,
    output wire [1:0]             m_axi_mm2s_arburst,
    input  wire                   m_axi_mm2s_rvalid,
    output wire                   m_axi_mm2s_rready,
    input  wire [MM_DATA_W-1:0]   m_axi_mm2s_rdata,
    input  wire [1:0]             m_axi_mm2s_rresp,
    input  wire                   m_axi_mm2s_rlast,
    // ---- M_AXI_S2MM (write-only) ----
    output wire                   m_axi_s2mm_awvalid,
    input  wire                   m_axi_s2mm_awready,
    output wire [MM_ADDR_W-1:0]   m_axi_s2mm_awaddr,
    output wire [7:0]             m_axi_s2mm_awlen,
    output wire [2:0]             m_axi_s2mm_awsize,
    output wire [1:0]             m_axi_s2mm_awburst,
    output wire                   m_axi_s2mm_wvalid,
    input  wire                   m_axi_s2mm_wready,
    output wire [MM_DATA_W-1:0]   m_axi_s2mm_wdata,
    output wire [MM_DATA_W/8-1:0] m_axi_s2mm_wstrb,
    output wire                   m_axi_s2mm_wlast,
    input  wire                   m_axi_s2mm_bvalid,
    output wire                   m_axi_s2mm_bready,
    input  wire [1:0]             m_axi_s2mm_bresp,
    // ---- M_AXI_SG (full, SG_DATA_W) ----
    output wire                   m_axi_sg_awvalid,
    input  wire                   m_axi_sg_awready,
    output wire [MM_ADDR_W-1:0]   m_axi_sg_awaddr,
    output wire [7:0]             m_axi_sg_awlen,
    output wire [2:0]             m_axi_sg_awsize,
    output wire [1:0]             m_axi_sg_awburst,
    output wire                   m_axi_sg_wvalid,
    input  wire                   m_axi_sg_wready,
    output wire [SG_DATA_W-1:0]   m_axi_sg_wdata,
    output wire [SG_DATA_W/8-1:0] m_axi_sg_wstrb,
    output wire                   m_axi_sg_wlast,
    input  wire                   m_axi_sg_bvalid,
    output wire                   m_axi_sg_bready,
    input  wire [1:0]             m_axi_sg_bresp,
    output wire                   m_axi_sg_arvalid,
    input  wire                   m_axi_sg_arready,
    output wire [MM_ADDR_W-1:0]   m_axi_sg_araddr,
    output wire [7:0]             m_axi_sg_arlen,
    output wire [2:0]             m_axi_sg_arsize,
    output wire [1:0]             m_axi_sg_arburst,
    input  wire                   m_axi_sg_rvalid,
    output wire                   m_axi_sg_rready,
    input  wire [SG_DATA_W-1:0]   m_axi_sg_rdata,
    input  wire [1:0]             m_axi_sg_rresp,
    input  wire                   m_axi_sg_rlast,

    // ---- S_AXI_DMA (AXI-Lite, 10b, no wstrb) ----
    input  wire                   s_axi_dma_awvalid,
    output wire                   s_axi_dma_awready,
    input  wire [9:0]             s_axi_dma_awaddr,
    input  wire                   s_axi_dma_wvalid,
    output wire                   s_axi_dma_wready,
    input  wire [31:0]            s_axi_dma_wdata,
    output wire                   s_axi_dma_bvalid,
    input  wire                   s_axi_dma_bready,
    output wire [1:0]             s_axi_dma_bresp,
    input  wire                   s_axi_dma_arvalid,
    output wire                   s_axi_dma_arready,
    input  wire [9:0]             s_axi_dma_araddr,
    output wire                   s_axi_dma_rvalid,
    input  wire                   s_axi_dma_rready,
    output wire [31:0]            s_axi_dma_rdata,
    output wire [1:0]             s_axi_dma_rresp,

    // ---- S_AXI_ETH (AXI-Lite, 18b, wstrb) ----
    input  wire                   s_axi_eth_awvalid,
    output wire                   s_axi_eth_awready,
    input  wire [17:0]            s_axi_eth_awaddr,
    input  wire                   s_axi_eth_wvalid,
    output wire                   s_axi_eth_wready,
    input  wire [31:0]            s_axi_eth_wdata,
    input  wire [3:0]             s_axi_eth_wstrb,
    output wire                   s_axi_eth_bvalid,
    input  wire                   s_axi_eth_bready,
    output wire [1:0]             s_axi_eth_bresp,
    input  wire                   s_axi_eth_arvalid,
    output wire                   s_axi_eth_arready,
    input  wire [17:0]            s_axi_eth_araddr,
    output wire                   s_axi_eth_rvalid,
    input  wire                   s_axi_eth_rready,
    output wire [31:0]            s_axi_eth_rdata,
    output wire [1:0]             s_axi_eth_rresp,

    // ---- interrupts ----
    output wire                   mm2s_introut,
    output wire                   s2mm_introut,
    output wire                   mac_irq,

    // ---- physical Ethernet pins (LVDS SGMII) ----
    input  wire                   lvds_clk_clk_p,
    input  wire                   lvds_clk_clk_n,
    input  wire                   sgmii_rxp,
    input  wire                   sgmii_rxn,
    output wire                   sgmii_txp,
    output wire                   sgmii_txn
);

    // ---- internal 32-bit AXIS between DMA and ETH -------------------------
    wire        tx_tvalid, tx_tready, tx_tlast;
    wire [31:0] tx_tdata;
    wire [3:0]  tx_tkeep;
    wire        rx_tvalid, rx_tready, rx_tlast;
    wire [31:0] rx_tdata;
    wire [3:0]  rx_tkeep;

    // =======================================================================
    // AXI DMA (standalone, SG enabled, MM=64b, stream=32b, addr=38)
    // =======================================================================
    vela_rtl_dma u_dma (
        .s_axi_lite_aclk   (axi_aclk),
        .m_axi_sg_aclk     (axi_aclk),
        .m_axi_mm2s_aclk   (axi_aclk),
        .m_axi_s2mm_aclk   (axi_aclk),
        .axi_resetn        (axi_aresetn),
        // control (AXI-Lite)
        .s_axi_lite_awvalid(s_axi_dma_awvalid), .s_axi_lite_awready(s_axi_dma_awready), .s_axi_lite_awaddr(s_axi_dma_awaddr),
        .s_axi_lite_wvalid (s_axi_dma_wvalid),  .s_axi_lite_wready (s_axi_dma_wready),  .s_axi_lite_wdata (s_axi_dma_wdata),
        .s_axi_lite_bvalid (s_axi_dma_bvalid),  .s_axi_lite_bready (s_axi_dma_bready),  .s_axi_lite_bresp (s_axi_dma_bresp),
        .s_axi_lite_arvalid(s_axi_dma_arvalid), .s_axi_lite_arready(s_axi_dma_arready), .s_axi_lite_araddr(s_axi_dma_araddr),
        .s_axi_lite_rvalid (s_axi_dma_rvalid),  .s_axi_lite_rready (s_axi_dma_rready),  .s_axi_lite_rdata (s_axi_dma_rdata), .s_axi_lite_rresp(s_axi_dma_rresp),
        // SG master (full)
        .m_axi_sg_awaddr(m_axi_sg_awaddr), .m_axi_sg_awlen(m_axi_sg_awlen), .m_axi_sg_awsize(m_axi_sg_awsize), .m_axi_sg_awburst(m_axi_sg_awburst),
        .m_axi_sg_awvalid(m_axi_sg_awvalid), .m_axi_sg_awready(m_axi_sg_awready),
        .m_axi_sg_wdata(m_axi_sg_wdata), .m_axi_sg_wstrb(m_axi_sg_wstrb), .m_axi_sg_wlast(m_axi_sg_wlast), .m_axi_sg_wvalid(m_axi_sg_wvalid), .m_axi_sg_wready(m_axi_sg_wready),
        .m_axi_sg_bresp(m_axi_sg_bresp), .m_axi_sg_bvalid(m_axi_sg_bvalid), .m_axi_sg_bready(m_axi_sg_bready),
        .m_axi_sg_araddr(m_axi_sg_araddr), .m_axi_sg_arlen(m_axi_sg_arlen), .m_axi_sg_arsize(m_axi_sg_arsize), .m_axi_sg_arburst(m_axi_sg_arburst),
        .m_axi_sg_arvalid(m_axi_sg_arvalid), .m_axi_sg_arready(m_axi_sg_arready),
        .m_axi_sg_rdata(m_axi_sg_rdata), .m_axi_sg_rresp(m_axi_sg_rresp), .m_axi_sg_rlast(m_axi_sg_rlast), .m_axi_sg_rvalid(m_axi_sg_rvalid), .m_axi_sg_rready(m_axi_sg_rready),
        // MM2S master (read) + stream
        .m_axi_mm2s_araddr(m_axi_mm2s_araddr), .m_axi_mm2s_arlen(m_axi_mm2s_arlen), .m_axi_mm2s_arsize(m_axi_mm2s_arsize), .m_axi_mm2s_arburst(m_axi_mm2s_arburst),
        .m_axi_mm2s_arvalid(m_axi_mm2s_arvalid), .m_axi_mm2s_arready(m_axi_mm2s_arready),
        .m_axi_mm2s_rdata(m_axi_mm2s_rdata), .m_axi_mm2s_rresp(m_axi_mm2s_rresp), .m_axi_mm2s_rlast(m_axi_mm2s_rlast), .m_axi_mm2s_rvalid(m_axi_mm2s_rvalid), .m_axi_mm2s_rready(m_axi_mm2s_rready),
        .m_axis_mm2s_tdata(tx_tdata), .m_axis_mm2s_tkeep(tx_tkeep), .m_axis_mm2s_tlast(tx_tlast), .m_axis_mm2s_tvalid(tx_tvalid), .m_axis_mm2s_tready(tx_tready),
        // S2MM master (write) + stream
        .m_axi_s2mm_awaddr(m_axi_s2mm_awaddr), .m_axi_s2mm_awlen(m_axi_s2mm_awlen), .m_axi_s2mm_awsize(m_axi_s2mm_awsize), .m_axi_s2mm_awburst(m_axi_s2mm_awburst),
        .m_axi_s2mm_awvalid(m_axi_s2mm_awvalid), .m_axi_s2mm_awready(m_axi_s2mm_awready),
        .m_axi_s2mm_wdata(m_axi_s2mm_wdata), .m_axi_s2mm_wstrb(m_axi_s2mm_wstrb), .m_axi_s2mm_wlast(m_axi_s2mm_wlast), .m_axi_s2mm_wvalid(m_axi_s2mm_wvalid), .m_axi_s2mm_wready(m_axi_s2mm_wready),
        .m_axi_s2mm_bresp(m_axi_s2mm_bresp), .m_axi_s2mm_bvalid(m_axi_s2mm_bvalid), .m_axi_s2mm_bready(m_axi_s2mm_bready),
        .s_axis_s2mm_tdata(rx_tdata), .s_axis_s2mm_tkeep(rx_tkeep), .s_axis_s2mm_tlast(rx_tlast), .s_axis_s2mm_tvalid(rx_tvalid), .s_axis_s2mm_tready(rx_tready),
        // interrupts
        .mm2s_introut(mm2s_introut), .s2mm_introut(s2mm_introut)
    );

    // =======================================================================
    // AXI 1G/2.5G Ethernet Subsystem (standalone, LVDS SGMII, DIFF_PAIR_2/0)
    // =======================================================================
    vela_rtl_eth u_eth (
        .s_axi_lite_clk(axi_aclk), .s_axi_lite_resetn(axi_aresetn), .axis_clk(axi_aclk),
        .axi_txd_arstn(axi_aresetn), .axi_txc_arstn(axi_aresetn), .axi_rxd_arstn(axi_aresetn), .axi_rxs_arstn(axi_aresetn),
        // control (AXI-Lite)
        .s_axi_awaddr(s_axi_eth_awaddr), .s_axi_awvalid(s_axi_eth_awvalid), .s_axi_awready(s_axi_eth_awready),
        .s_axi_wdata(s_axi_eth_wdata), .s_axi_wstrb(s_axi_eth_wstrb), .s_axi_wvalid(s_axi_eth_wvalid), .s_axi_wready(s_axi_eth_wready),
        .s_axi_bresp(s_axi_eth_bresp), .s_axi_bvalid(s_axi_eth_bvalid), .s_axi_bready(s_axi_eth_bready),
        .s_axi_araddr(s_axi_eth_araddr), .s_axi_arvalid(s_axi_eth_arvalid), .s_axi_arready(s_axi_eth_arready),
        .s_axi_rdata(s_axi_eth_rdata), .s_axi_rresp(s_axi_eth_rresp), .s_axi_rvalid(s_axi_eth_rvalid), .s_axi_rready(s_axi_eth_rready),
        // TX/RX data streams
        .s_axis_txd_tdata(tx_tdata), .s_axis_txd_tkeep(tx_tkeep), .s_axis_txd_tlast(tx_tlast), .s_axis_txd_tvalid(tx_tvalid), .s_axis_txd_tready(tx_tready),
        .m_axis_rxd_tdata(rx_tdata), .m_axis_rxd_tkeep(rx_tkeep), .m_axis_rxd_tlast(rx_tlast), .m_axis_rxd_tvalid(rx_tvalid), .m_axis_rxd_tready(rx_tready),
        // TX control / RX status streams -- parked (DMA has no cntrl/sts stream)
        .s_axis_txc_tvalid(1'b0),
        .m_axis_rxs_tready(1'b1),
        // MDIO -- parked (PHY management deferred)
        .mdio_mdio_i(1'b1),
        // status / interrupt
        .signal_detect(1'b1),
        .interrupt(mac_irq),
        // SGMII + 125 MHz LVDS reference clock
        .lvds_clk_clk_p(lvds_clk_clk_p), .lvds_clk_clk_n(lvds_clk_clk_n),
        .sgmii_txp(sgmii_txp), .sgmii_txn(sgmii_txn), .sgmii_rxp(sgmii_rxp), .sgmii_rxn(sgmii_rxn),
        // LVDS RIU / bitslice-ready tie-offs (identical to the block design)
        .riu_prsnt_1(1'b0), .riu_prsnt_2(1'b0), .riu_prsnt_3(1'b0),
        .riu_valid_1(1'b0), .riu_valid_2(1'b0), .riu_valid_3(1'b0),
        .riu_rddata_1(16'b0), .riu_rddata_2(16'b0), .riu_rddata_3(16'b0),
        .rx_dly_rdy_1(1'b1), .rx_dly_rdy_2(1'b1), .rx_dly_rdy_3(1'b1),
        .rx_vtc_rdy_1(1'b1), .rx_vtc_rdy_2(1'b1), .rx_vtc_rdy_3(1'b1),
        .tx_dly_rdy_1(1'b1), .tx_dly_rdy_2(1'b1), .tx_dly_rdy_3(1'b1),
        .tx_vtc_rdy_1(1'b1), .tx_vtc_rdy_2(1'b1), .tx_vtc_rdy_3(1'b1)
    );

endmodule
