// =============================================================================
// VelaAxiEth_Wrapper.sv
//
// SystemVerilog wrapper backing the Chisel BlackBox `VelaAxiEthBlackBox`
// (generators/velaeth/src/main/scala/VelaAxiEth.scala).
//
// Instantiates and connects THREE Vivado IP cores:
//   * u_axi_dma  : "AXI Direct Memory Access"        (axi_dma_0,          SG enabled)
//   * u_axi_eth  : "AXI 1G/2.5G Ethernet Subsystem"  (axi_ethernet_0,     SGMII)
//   * u_smc      : "AXI Interconnect" (RTL v2.1)     (axi_interconnect_0, 3 slaves -> 1 master)
//
// Datapath:
//   DMA M_AXIS_MM2S   --> ETH s_axis_txd      (transmit stream)
//   ETH  M_AXIS_RXD   --> DMA S_AXIS_S2MM     (receive stream)
//   DMA {M_AXI_MM2S, M_AXI_S2MM, M_AXI_SG} --> AXI Interconnect --> m_axi_mm (to TileLink)
//   DMA S_AXI_LITE    <-- s_axi_dma_* (AXI4 down-adapted to AXI-Lite, addr masked)
//   ETH S_AXI         <-- s_axi_eth_* (AXI4 down-adapted to AXI-Lite, addr masked)
//   mm2s/s2mm/mac IRQ --> PLIC
//
// !!! IP-RECONCILIATION NOTE !!!
//   Instance module names (axi_dma_0 / axi_ethernet_0 / axi_interconnect_0) and
//   their exact PORT names depend on the IP version/options you generate. Create
//   the three IP in Vivado with the config noted at each instance, add the .xci
//   to the project, then reconcile any port-name differences below. The wrapper
//   logic (AXI-Lite down-adaptation, address masking, AXIS crossing, the 3->1
//   merge topology) is complete and does not change.
//
//   AXI Interconnect config: 3 slave IFs (S00=MM2S, S01=S2MM, S02=SG), 1 master
//   IF (M00), M00 data width = MM_DATA_W, addr = MM_ADDR_W. Per-port clocks all
//   tied to axi_aclk. M00 ID width (IC_ID_W) bridged up to MM_ID_W below.
// =============================================================================

`define VELA_ETH_BASE 32'h6420_0000   // must match VelaAxiEthParams.ethCtrlAddress
`define VELA_DMA_BASE 32'h6410_0000   // must match VelaAxiEthParams.dmaCtrlAddress

module VelaAxiEthBlackBox #(
    parameter MM_DATA_W = 64,
    parameter MM_ADDR_W = 38,
    parameter MM_ID_W   = 4,
    parameter C_ADDR_W  = 32,
    parameter SG_DATA_W = 32          // AXI DMA scatter-gather master data width
) (
    // ---- clock / reset -------------------------------------------------------
    input  wire                   axi_aclk,
    input  wire                   axi_aresetn,

    // ---- AXI DMA register slave (from CPU, AXI4 -> AXI-Lite) -----------------
    input  wire                   s_axi_dma_awvalid,
    output wire                   s_axi_dma_awready,
    input  wire [C_ADDR_W-1:0]    s_axi_dma_awaddr,
    input  wire                   s_axi_dma_wvalid,
    output wire                   s_axi_dma_wready,
    input  wire [31:0]            s_axi_dma_wdata,
    input  wire [3:0]             s_axi_dma_wstrb,
    output wire                   s_axi_dma_bvalid,
    input  wire                   s_axi_dma_bready,
    output wire [1:0]             s_axi_dma_bresp,
    input  wire                   s_axi_dma_arvalid,
    output wire                   s_axi_dma_arready,
    input  wire [C_ADDR_W-1:0]    s_axi_dma_araddr,
    output wire                   s_axi_dma_rvalid,
    input  wire                   s_axi_dma_rready,
    output wire [31:0]            s_axi_dma_rdata,
    output wire [1:0]             s_axi_dma_rresp,

    // ---- AXI Ethernet register slave (from CPU, AXI4 -> AXI-Lite) ------------
    input  wire                   s_axi_eth_awvalid,
    output wire                   s_axi_eth_awready,
    input  wire [C_ADDR_W-1:0]    s_axi_eth_awaddr,
    input  wire                   s_axi_eth_wvalid,
    output wire                   s_axi_eth_wready,
    input  wire [31:0]            s_axi_eth_wdata,
    input  wire [3:0]             s_axi_eth_wstrb,
    output wire                   s_axi_eth_bvalid,
    input  wire                   s_axi_eth_bready,
    output wire [1:0]             s_axi_eth_bresp,
    input  wire                   s_axi_eth_arvalid,
    output wire                   s_axi_eth_arready,
    input  wire [C_ADDR_W-1:0]    s_axi_eth_araddr,
    output wire                   s_axi_eth_rvalid,
    input  wire                   s_axi_eth_rready,
    output wire [31:0]            s_axi_eth_rdata,
    output wire [1:0]             s_axi_eth_rresp,

    // ---- merged AXI4 memory master (Interconnect M00 -> TileLink) ------------
    output wire                   m_axi_mm_awvalid,
    input  wire                   m_axi_mm_awready,
    output wire [MM_ID_W-1:0]     m_axi_mm_awid,
    output wire [MM_ADDR_W-1:0]   m_axi_mm_awaddr,
    output wire [7:0]             m_axi_mm_awlen,
    output wire [2:0]             m_axi_mm_awsize,
    output wire [1:0]             m_axi_mm_awburst,
    output wire                   m_axi_mm_wvalid,
    input  wire                   m_axi_mm_wready,
    output wire [MM_DATA_W-1:0]   m_axi_mm_wdata,
    output wire [MM_DATA_W/8-1:0] m_axi_mm_wstrb,
    output wire                   m_axi_mm_wlast,
    input  wire                   m_axi_mm_bvalid,
    output wire                   m_axi_mm_bready,
    input  wire [MM_ID_W-1:0]     m_axi_mm_bid,
    input  wire [1:0]             m_axi_mm_bresp,
    output wire                   m_axi_mm_arvalid,
    input  wire                   m_axi_mm_arready,
    output wire [MM_ID_W-1:0]     m_axi_mm_arid,
    output wire [MM_ADDR_W-1:0]   m_axi_mm_araddr,
    output wire [7:0]             m_axi_mm_arlen,
    output wire [2:0]             m_axi_mm_arsize,
    output wire [1:0]             m_axi_mm_arburst,
    input  wire                   m_axi_mm_rvalid,
    output wire                   m_axi_mm_rready,
    input  wire [MM_ID_W-1:0]     m_axi_mm_rid,
    input  wire [MM_DATA_W-1:0]   m_axi_mm_rdata,
    input  wire [1:0]             m_axi_mm_rresp,
    input  wire                   m_axi_mm_rlast,

    // ---- interrupts ----------------------------------------------------------
    output wire                   mm2s_introut,
    output wire                   s2mm_introut,
    output wire                   mac_irq,

    // ---- physical Ethernet pins ---------------------------------------------
    input  wire                   gt_refclk_p,
    input  wire                   gt_refclk_n,
    input  wire                   sgmii_rxp,
    input  wire                   sgmii_rxn,
    output wire                   sgmii_txp,
    output wire                   sgmii_txn,
    output wire                   link_up,
    output wire                   speed_is_100,
    output wire                   speed_is_10_100
);

    // ------------------------------------------------------------------------
    // Local (offset) addresses for each AXI-Lite slave. TLToAXI4 emits the
    // *absolute* system address; the Xilinx cores want a local register offset.
    // ------------------------------------------------------------------------
    wire [C_ADDR_W-1:0] dma_awaddr_local = s_axi_dma_awaddr - `VELA_DMA_BASE;
    wire [C_ADDR_W-1:0] dma_araddr_local = s_axi_dma_araddr - `VELA_DMA_BASE;
    wire [C_ADDR_W-1:0] eth_awaddr_local = s_axi_eth_awaddr - `VELA_ETH_BASE;
    wire [C_ADDR_W-1:0] eth_araddr_local = s_axi_eth_araddr - `VELA_ETH_BASE;

    // ------------------------------------------------------------------------
    // Internal AXI4-Stream between DMA and Ethernet (8-byte, 1G/2.5G)
    // ------------------------------------------------------------------------
    wire        tx_tvalid, tx_tready, tx_tlast;
    wire [63:0] tx_tdata;
    wire [7:0]  tx_tkeep;

    wire        rx_tvalid, rx_tready, rx_tlast;
    wire [63:0] rx_tdata;
    wire [7:0]  rx_tkeep;

    // ------------------------------------------------------------------------
    // Intermediate AXI buses: DMA's three MM masters -> Interconnect slaves
    //   S00 = M_AXI_MM2S (read-only), S01 = M_AXI_S2MM (write-only), S02 = M_AXI_SG (full)
    // ------------------------------------------------------------------------
    // MM2S (read-only)
    wire                   mm2s_arvalid, mm2s_arready, mm2s_rvalid, mm2s_rready, mm2s_rlast;
    wire [MM_ADDR_W-1:0]   mm2s_araddr;
    wire [7:0]             mm2s_arlen;
    wire [2:0]             mm2s_arsize;
    wire [1:0]             mm2s_arburst, mm2s_rresp;
    wire [3:0]             mm2s_arcache;
    wire [2:0]             mm2s_arprot;
    wire [MM_DATA_W-1:0]   mm2s_rdata;
    // S2MM (write-only)
    wire                   s2mm_awvalid, s2mm_awready, s2mm_wvalid, s2mm_wready, s2mm_wlast, s2mm_bvalid, s2mm_bready;
    wire [MM_ADDR_W-1:0]   s2mm_awaddr;
    wire [7:0]             s2mm_awlen;
    wire [2:0]             s2mm_awsize;
    wire [1:0]             s2mm_awburst, s2mm_bresp;
    wire [3:0]             s2mm_awcache;
    wire [2:0]             s2mm_awprot;
    wire [MM_DATA_W-1:0]   s2mm_wdata;
    wire [MM_DATA_W/8-1:0] s2mm_wstrb;
    // SG (full, narrow data)
    wire                   sg_awvalid, sg_awready, sg_wvalid, sg_wready, sg_wlast, sg_bvalid, sg_bready;
    wire                   sg_arvalid, sg_arready, sg_rvalid, sg_rready, sg_rlast;
    wire [MM_ADDR_W-1:0]   sg_awaddr, sg_araddr;
    wire [7:0]             sg_awlen, sg_arlen;
    wire [2:0]             sg_awsize, sg_arsize, sg_awprot, sg_arprot;
    wire [1:0]             sg_awburst, sg_arburst, sg_bresp, sg_rresp;
    wire [3:0]             sg_awcache, sg_arcache;
    wire [SG_DATA_W-1:0]   sg_wdata, sg_rdata;
    wire [SG_DATA_W/8-1:0] sg_wstrb;

    // ========================================================================
    // AXI DMA (axi_dma_0).  Config: SG engine ON, Multichannel OFF, MicroDMA OFF,
    // buffer-length reg >= 16 bits, addr width = MM_ADDR_W, MM data = MM_DATA_W,
    // stream data = 64, SG data = SG_DATA_W. All clocks tied to axi_aclk.
    // ========================================================================
    axi_dma_0 u_axi_dma (
        .s_axi_lite_aclk    (axi_aclk),
        .m_axi_sg_aclk      (axi_aclk),
        .m_axi_mm2s_aclk    (axi_aclk),
        .m_axi_s2mm_aclk    (axi_aclk),
        .axi_resetn         (axi_aresetn),

        // ---- control (AXI-Lite) ----
        .s_axi_lite_awvalid (s_axi_dma_awvalid),
        .s_axi_lite_awready (s_axi_dma_awready),
        .s_axi_lite_awaddr  (dma_awaddr_local[9:0]),
        .s_axi_lite_wvalid  (s_axi_dma_wvalid),
        .s_axi_lite_wready  (s_axi_dma_wready),
        .s_axi_lite_wdata   (s_axi_dma_wdata),
        .s_axi_lite_bvalid  (s_axi_dma_bvalid),
        .s_axi_lite_bready  (s_axi_dma_bready),
        .s_axi_lite_bresp   (s_axi_dma_bresp),
        .s_axi_lite_arvalid (s_axi_dma_arvalid),
        .s_axi_lite_arready (s_axi_dma_arready),
        .s_axi_lite_araddr  (dma_araddr_local[9:0]),
        .s_axi_lite_rvalid  (s_axi_dma_rvalid),
        .s_axi_lite_rready  (s_axi_dma_rready),
        .s_axi_lite_rdata   (s_axi_dma_rdata),
        .s_axi_lite_rresp   (s_axi_dma_rresp),

        // ---- M_AXI_SG (descriptor fetch/update) ----
        .m_axi_sg_awaddr    (sg_awaddr),  .m_axi_sg_awlen (sg_awlen),  .m_axi_sg_awsize (sg_awsize),
        .m_axi_sg_awburst   (sg_awburst), .m_axi_sg_awprot(sg_awprot), .m_axi_sg_awcache(sg_awcache),
        .m_axi_sg_awvalid   (sg_awvalid), .m_axi_sg_awready(sg_awready),
        .m_axi_sg_wdata     (sg_wdata),   .m_axi_sg_wstrb (sg_wstrb),  .m_axi_sg_wlast  (sg_wlast),
        .m_axi_sg_wvalid    (sg_wvalid),  .m_axi_sg_wready(sg_wready),
        .m_axi_sg_bresp     (sg_bresp),   .m_axi_sg_bvalid(sg_bvalid), .m_axi_sg_bready (sg_bready),
        .m_axi_sg_araddr    (sg_araddr),  .m_axi_sg_arlen (sg_arlen),  .m_axi_sg_arsize (sg_arsize),
        .m_axi_sg_arburst   (sg_arburst), .m_axi_sg_arprot(sg_arprot), .m_axi_sg_arcache(sg_arcache),
        .m_axi_sg_arvalid   (sg_arvalid), .m_axi_sg_arready(sg_arready),
        .m_axi_sg_rdata     (sg_rdata),   .m_axi_sg_rresp (sg_rresp),  .m_axi_sg_rlast  (sg_rlast),
        .m_axi_sg_rvalid    (sg_rvalid),  .m_axi_sg_rready(sg_rready),

        // ---- M_AXI_MM2S (read TX packet payload from DRAM) ----
        .m_axi_mm2s_araddr  (mm2s_araddr),  .m_axi_mm2s_arlen (mm2s_arlen),  .m_axi_mm2s_arsize (mm2s_arsize),
        .m_axi_mm2s_arburst (mm2s_arburst), .m_axi_mm2s_arprot(mm2s_arprot), .m_axi_mm2s_arcache(mm2s_arcache),
        .m_axi_mm2s_arvalid (mm2s_arvalid), .m_axi_mm2s_arready(mm2s_arready),
        .m_axi_mm2s_rdata   (mm2s_rdata),   .m_axi_mm2s_rresp (mm2s_rresp),  .m_axi_mm2s_rlast  (mm2s_rlast),
        .m_axi_mm2s_rvalid  (mm2s_rvalid),  .m_axi_mm2s_rready(mm2s_rready),
        // ---- M_AXIS_MM2S (TX stream -> Ethernet) ----
        .m_axis_mm2s_tdata  (tx_tdata),  .m_axis_mm2s_tkeep (tx_tkeep), .m_axis_mm2s_tlast (tx_tlast),
        .m_axis_mm2s_tvalid (tx_tvalid), .m_axis_mm2s_tready(tx_tready),

        // ---- M_AXI_S2MM (write RX packet payload to DRAM) ----
        .m_axi_s2mm_awaddr  (s2mm_awaddr),  .m_axi_s2mm_awlen (s2mm_awlen),  .m_axi_s2mm_awsize (s2mm_awsize),
        .m_axi_s2mm_awburst (s2mm_awburst), .m_axi_s2mm_awprot(s2mm_awprot), .m_axi_s2mm_awcache(s2mm_awcache),
        .m_axi_s2mm_awvalid (s2mm_awvalid), .m_axi_s2mm_awready(s2mm_awready),
        .m_axi_s2mm_wdata   (s2mm_wdata),   .m_axi_s2mm_wstrb (s2mm_wstrb),  .m_axi_s2mm_wlast  (s2mm_wlast),
        .m_axi_s2mm_wvalid  (s2mm_wvalid),  .m_axi_s2mm_wready(s2mm_wready),
        .m_axi_s2mm_bresp   (s2mm_bresp),   .m_axi_s2mm_bvalid(s2mm_bvalid), .m_axi_s2mm_bready (s2mm_bready),
        // ---- S_AXIS_S2MM (RX stream <- Ethernet) ----
        .s_axis_s2mm_tdata  (rx_tdata),  .s_axis_s2mm_tkeep (rx_tkeep), .s_axis_s2mm_tlast (rx_tlast),
        .s_axis_s2mm_tvalid (rx_tvalid), .s_axis_s2mm_tready(rx_tready),

        // ---- interrupts ----
        .mm2s_introut       (mm2s_introut),
        .s2mm_introut       (s2mm_introut)
    );

    // ========================================================================
    // AXI 1G/2.5G Ethernet Subsystem (axi_ethernet_0).  Config: PHY = SGMII,
    // MDIO enabled, shared-logic-in-core (derives clocks from gt_refclk),
    // checksum offload OFF (no txc/rxs streams). Register clock = axi_aclk.
    // ========================================================================
    axi_ethernet_0 u_axi_eth (
        .s_axi_lite_clk     (axi_aclk),
        .s_axi_lite_resetn   (axi_aresetn),
        .axis_clk           (axi_aclk),

        // ---- control (AXI-Lite) ----
        .s_axi_awaddr       (eth_awaddr_local[17:0]),
        .s_axi_awvalid      (s_axi_eth_awvalid),
        .s_axi_awready      (s_axi_eth_awready),
        .s_axi_wdata        (s_axi_eth_wdata),
        .s_axi_wstrb        (s_axi_eth_wstrb),
        .s_axi_wvalid       (s_axi_eth_wvalid),
        .s_axi_wready       (s_axi_eth_wready),
        .s_axi_bresp        (s_axi_eth_bresp),
        .s_axi_bvalid       (s_axi_eth_bvalid),
        .s_axi_bready       (s_axi_eth_bready),
        .s_axi_araddr       (eth_araddr_local[17:0]),
        .s_axi_arvalid      (s_axi_eth_arvalid),
        .s_axi_arready      (s_axi_eth_arready),
        .s_axi_rdata        (s_axi_eth_rdata),
        .s_axi_rresp        (s_axi_eth_rresp),
        .s_axi_rvalid       (s_axi_eth_rvalid),
        .s_axi_rready       (s_axi_eth_rready),

        // ---- TX data stream (from DMA MM2S) ----
        .s_axis_txd_tdata   (tx_tdata),  .s_axis_txd_tkeep (tx_tkeep), .s_axis_txd_tlast (tx_tlast),
        .s_axis_txd_tvalid  (tx_tvalid), .s_axis_txd_tready(tx_tready),
        // ---- RX data stream (to DMA S2MM) ----
        .m_axis_rxd_tdata   (rx_tdata),  .m_axis_rxd_tkeep (rx_tkeep), .m_axis_rxd_tlast (rx_tlast),
        .m_axis_rxd_tvalid  (rx_tvalid), .m_axis_rxd_tready(rx_tready),

        // ---- TX control / RX status streams (present in this IP config) ----
        // The DMA here has no cntrl/sts stream (c_sg_include_stscntrl_strm=0),
        // so park them: send no TX control word, drain the RX status stream.
        .s_axis_txc_tvalid  (1'b0),
        .m_axis_rxs_tready  (1'b1),

        // ---- MDIO (PHY management) ----
        // mdio_mdc/o/t are outputs, mdio_mdio_i an input; route them to a board
        // MDIO pin via an IOBUF for the driver to manage the PHY. Parked here.
        .mdio_mdio_i        (1'b1),

        // ---- SGMII + 125 MHz LVDS reference clock (NOT gtref_* on the LVDS variant) ----
        .lvds_clk_clk_p     (gt_refclk_p),
        .lvds_clk_clk_n     (gt_refclk_n),
        .sgmii_txp          (sgmii_txp),
        .sgmii_txn          (sgmii_txn),
        .sgmii_rxp          (sgmii_rxp),
        .sgmii_rxn          (sgmii_rxn),
        // .phy_rst_n is an IP OUTPUT (reset to the external PHY) -> route to a
        // board pin for a real PHY; left open here.

        // ---- status / interrupt (mac_irq AND interrupt are both outputs; use one) ----
        .interrupt          (mac_irq),
        .signal_detect      (1'b1)

        // NOTE: this IP config (LVDS, shared-logic-in-core) also exposes bitslice
        // control (riu_*, tx_dly_rdy_*, tx_vtc_rdy_*, clk125_out, ...). They are
        // left open here so the design elaborates, but the SGMII link will NOT
        // come up until they are driven -- open the IP Example Design to see the
        // correct top-level connections and mirror them here.
    );

    // Link-status LEDs: derive from the IP status vector if exposed, else park.
    // (The 1G/2.5G subsystem exposes link status via `status_vector`/registers;
    //  wire those here if your config brings them out.)
    assign link_up         = 1'b0;
    assign speed_is_100    = 1'b0;
    assign speed_is_10_100 = 1'b0;

    // ========================================================================
    // AXI Interconnect (axi_interconnect_0, RTL v2.1): 3 slaves -> 1 master.
    // Used instead of SmartConnect because this FPGA flow synthesizes every IP
    // inline; SmartConnect is a nested block design that needs OOC synthesis
    // (DRC INBB-3 / missing .dcp). Config: NUM_SI=3, NUM_MI=1, single clock.
    //
    // The interconnect adds ceil(log2(3))=2 arbitration bits to the (ID-less)
    // DMA masters, so M00's ID is IC_ID_W bits. Zero-extend it up to the
    // blackbox m_axi_mm ID width (MM_ID_W) on the request path and truncate on
    // the response path. If the generated axi_interconnect_0 reports a different
    // M00 ID width, update IC_ID_W to match.
    // ========================================================================
    localparam IC_ID_W = 2;
    wire [IC_ID_W-1:0] ic_awid, ic_arid, ic_bid, ic_rid;
    assign m_axi_mm_awid = {{(MM_ID_W-IC_ID_W){1'b0}}, ic_awid};
    assign m_axi_mm_arid = {{(MM_ID_W-IC_ID_W){1'b0}}, ic_arid};
    assign ic_bid = m_axi_mm_bid[IC_ID_W-1:0];
    assign ic_rid = m_axi_mm_rid[IC_ID_W-1:0];

    axi_interconnect_0 u_smc (
        .INTERCONNECT_ACLK    (axi_aclk),
        .INTERCONNECT_ARESETN (axi_aresetn),
        .S00_ACLK  (axi_aclk), .S00_ARESETN (axi_aresetn),
        .S01_ACLK  (axi_aclk), .S01_ARESETN (axi_aresetn),
        .S02_ACLK  (axi_aclk), .S02_ARESETN (axi_aresetn),
        .M00_ACLK  (axi_aclk), .M00_ARESETN (axi_aresetn),

        // ---- S00 = MM2S (read-only master; write channels tied off) ----
        .S00_AXI_araddr  (mm2s_araddr),  .S00_AXI_arlen (mm2s_arlen),  .S00_AXI_arsize (mm2s_arsize),
        .S00_AXI_arburst (mm2s_arburst), .S00_AXI_arprot(mm2s_arprot), .S00_AXI_arcache(mm2s_arcache),
        .S00_AXI_arvalid (mm2s_arvalid), .S00_AXI_arready(mm2s_arready),
        .S00_AXI_rdata   (mm2s_rdata),   .S00_AXI_rresp (mm2s_rresp),  .S00_AXI_rlast  (mm2s_rlast),
        .S00_AXI_rvalid  (mm2s_rvalid),  .S00_AXI_rready(mm2s_rready),
        .S00_AXI_awvalid (1'b0), .S00_AXI_wvalid(1'b0), .S00_AXI_bready(1'b1),

        // ---- S01 = S2MM (write-only master; read channels tied off) ----
        .S01_AXI_awaddr  (s2mm_awaddr),  .S01_AXI_awlen (s2mm_awlen),  .S01_AXI_awsize (s2mm_awsize),
        .S01_AXI_awburst (s2mm_awburst), .S01_AXI_awprot(s2mm_awprot), .S01_AXI_awcache(s2mm_awcache),
        .S01_AXI_awvalid (s2mm_awvalid), .S01_AXI_awready(s2mm_awready),
        .S01_AXI_wdata   (s2mm_wdata),   .S01_AXI_wstrb (s2mm_wstrb),  .S01_AXI_wlast  (s2mm_wlast),
        .S01_AXI_wvalid  (s2mm_wvalid),  .S01_AXI_wready(s2mm_wready),
        .S01_AXI_bresp   (s2mm_bresp),   .S01_AXI_bvalid(s2mm_bvalid), .S01_AXI_bready (s2mm_bready),
        .S01_AXI_arvalid (1'b0), .S01_AXI_rready(1'b1),

        // ---- S02 = SG (full) ----
        .S02_AXI_awaddr  (sg_awaddr),  .S02_AXI_awlen (sg_awlen),  .S02_AXI_awsize (sg_awsize),
        .S02_AXI_awburst (sg_awburst), .S02_AXI_awprot(sg_awprot), .S02_AXI_awcache(sg_awcache),
        .S02_AXI_awvalid (sg_awvalid), .S02_AXI_awready(sg_awready),
        .S02_AXI_wdata   (sg_wdata),   .S02_AXI_wstrb (sg_wstrb),  .S02_AXI_wlast  (sg_wlast),
        .S02_AXI_wvalid  (sg_wvalid),  .S02_AXI_wready(sg_wready),
        .S02_AXI_bresp   (sg_bresp),   .S02_AXI_bvalid(sg_bvalid), .S02_AXI_bready (sg_bready),
        .S02_AXI_araddr  (sg_araddr),  .S02_AXI_arlen (sg_arlen),  .S02_AXI_arsize (sg_arsize),
        .S02_AXI_arburst (sg_arburst), .S02_AXI_arprot(sg_arprot), .S02_AXI_arcache(sg_arcache),
        .S02_AXI_arvalid (sg_arvalid), .S02_AXI_arready(sg_arready),
        .S02_AXI_rdata   (sg_rdata),   .S02_AXI_rresp (sg_rresp),  .S02_AXI_rlast  (sg_rlast),
        .S02_AXI_rvalid  (sg_rvalid),  .S02_AXI_rready(sg_rready),

        // ---- M00 = merged master -> TileLink (blackbox m_axi_mm) ----
        // awid/arid/bid/rid go through the IC_ID_W<->MM_ID_W bridge wires above.
        .M00_AXI_awid    (ic_awid),          .M00_AXI_awaddr (m_axi_mm_awaddr), .M00_AXI_awlen (m_axi_mm_awlen),
        .M00_AXI_awsize  (m_axi_mm_awsize),  .M00_AXI_awburst(m_axi_mm_awburst),
        .M00_AXI_awvalid (m_axi_mm_awvalid), .M00_AXI_awready(m_axi_mm_awready),
        .M00_AXI_wdata   (m_axi_mm_wdata),   .M00_AXI_wstrb  (m_axi_mm_wstrb),  .M00_AXI_wlast (m_axi_mm_wlast),
        .M00_AXI_wvalid  (m_axi_mm_wvalid),  .M00_AXI_wready (m_axi_mm_wready),
        .M00_AXI_bid     (ic_bid),           .M00_AXI_bresp  (m_axi_mm_bresp),
        .M00_AXI_bvalid  (m_axi_mm_bvalid),  .M00_AXI_bready (m_axi_mm_bready),
        .M00_AXI_arid    (ic_arid),          .M00_AXI_araddr (m_axi_mm_araddr), .M00_AXI_arlen (m_axi_mm_arlen),
        .M00_AXI_arsize  (m_axi_mm_arsize),  .M00_AXI_arburst(m_axi_mm_arburst),
        .M00_AXI_arvalid (m_axi_mm_arvalid), .M00_AXI_arready(m_axi_mm_arready),
        .M00_AXI_rid     (ic_rid),           .M00_AXI_rdata  (m_axi_mm_rdata),  .M00_AXI_rresp (m_axi_mm_rresp),
        .M00_AXI_rlast   (m_axi_mm_rlast),   .M00_AXI_rvalid (m_axi_mm_rvalid), .M00_AXI_rready(m_axi_mm_rready)
    );

endmodule
