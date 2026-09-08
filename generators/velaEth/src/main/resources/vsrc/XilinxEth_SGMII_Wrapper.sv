// XilinxEth_SGMII_Wrapper.sv
// Complete wrapper for Xilinx 1G Ethernet with SGMII interface
// Converts between AXI4-Stream (64-bit) and GMII interface of Xilinx IP
// For VCU118 RJ45 Ethernet port

//module XilinxEth_SGMII_Wrapper (
module XilinxEthBlackBox (
    // System Reset (no separate sys_clk needed - IP generates clocks from gt_refclk)
    input  wire        sys_rst,          // Active-high reset

    // MGT Reference Clock (typically 125MHz for 1G Ethernet)
    // IP generates userclk2/rxuserclk2 internally from this
    input  wire        gt_refclk_p,
    input  wire        gt_refclk_n,

    // AXI4-Stream TX Interface (64-bit, from system)
    input  wire        s_axis_tx_valid,
    input  wire [63:0] s_axis_tx_data,
    input  wire [7:0]  s_axis_tx_keep,
    input  wire        s_axis_tx_last,
    output reg         s_axis_tx_ready,

    // AXI4-Stream RX Interface (64-bit, to system)
    output reg         m_axis_rx_valid,
    output reg  [63:0] m_axis_rx_data,
    output reg  [7:0]  m_axis_rx_keep,
    output reg         m_axis_rx_last,
    input  wire        m_axis_rx_ready,

    // SGMII Physical Interface
    output wire        sgmii_txp,
    output wire        sgmii_txn,
    input  wire        sgmii_rxp,
    input  wire        sgmii_rxn,

    // Status signals
    output wire        link_up,
    output wire        speed_is_100,
    output wire        speed_is_10_100
);

    // Internal signals for GMII interface
    (* mark_debug = "true" *) wire [7:0]  gmii_txd;
    (* mark_debug = "true" *) wire        gmii_tx_en;
    (* mark_debug = "true" *) wire        gmii_tx_er;
    (* mark_debug = "true" *) wire [7:0]  gmii_rxd;
    (* mark_debug = "true" *) wire        gmii_rx_dv;
    (* mark_debug = "true" *) wire        gmii_rx_er;

    // Clock signal from PCS/PMA (125MHz for both TX and RX)
    wire        clk125;

    // Status
    (* mark_debug = "true" *) wire        resetdone;
    (* mark_debug = "true" *) wire [15:0] status_vector;

    // ================================================================
    // Instantiate Xilinx 1G/2.5G Ethernet PCS/PMA IP (with LVDS)
    // ================================================================
    // Generated IP: eth_1g_sgmii_ip (gig_ethernet_pcs_pma v16.2)
    // Uses LVDS transceivers (not GT) for SGMII
    //
    // The IP only exists in the Vivado project, so for Verilator simulation
    // (VERILATOR is predefined by the tool) substitute a behavioral model:
    // clk125 comes straight from gt_refclk_p and GMII is looped back TX->RX,
    // so if the harness drives a real 125MHz refclk the NIC sees its own
    // frames; with the refclk tied off the block is simply inert.

`ifdef VERILATOR
    assign clk125        = gt_refclk_p;
    assign resetdone     = 1'b1;
    assign status_vector = 16'h0001;    // [0] link_up
    assign gmii_rxd      = gmii_txd;
    assign gmii_rx_dv    = gmii_tx_en;
    assign gmii_rx_er    = 1'b0;
    assign sgmii_txp     = 1'b0;
    assign sgmii_txn     = 1'b1;
`else
    eth_1g_sgmii_ip eth_ip_inst (
        // LVDS Transceiver Interface - all ports have _0 suffix
        .txp_0                (sgmii_txp),        // TX differential positive
        .txn_0                (sgmii_txn),        // TX differential negative
        .rxp_0                (sgmii_rxp),        // RX differential positive
        .rxn_0                (sgmii_rxn),        // RX differential negative
        .signal_detect_0      (1'b1),             // Signal detect (always present)

        // Reference Clock (125MHz differential)
        .refclk125_p          (gt_refclk_p),      // 125MHz reference clock positive
        .refclk125_n          (gt_refclk_n),      // 125MHz reference clock negative

        // Clock Outputs
        .clk125_out           (clk125),           // 125MHz clock for GMII (used for TX/RX)
        .clk312_out           (),                 // 312.5MHz clock (not used)
        .rst_125_out          (),                 // 125MHz domain reset
        .tx_logic_reset       (),                 // TX logic reset
        .rx_logic_reset       (),                 // RX logic reset
        .rx_locked            (resetdone),        // RX PLL locked (used as resetdone)
        .tx_locked            (),                 // TX PLL locked

        // GMII Interface (all ports have _0 suffix)
        .gmii_txd_0           (gmii_txd),         // TX data [7:0]
        .gmii_tx_en_0         (gmii_tx_en),       // TX enable
        .gmii_tx_er_0         (gmii_tx_er),       // TX error
        .gmii_rxd_0           (gmii_rxd),         // RX data [7:0]
        .gmii_rx_dv_0         (gmii_rx_dv),       // RX data valid
        .gmii_rx_er_0         (gmii_rx_er),       // RX error
        .gmii_isolate_0       (),                 // GMII isolate

        // SGMII Clock outputs for MAC
        .sgmii_clk_r_0        (),                 // Rising edge aligned clock
        .sgmii_clk_f_0        (),                 // Falling edge aligned clock
        .sgmii_clk_en_0       (),                 // Clock enable

        // Speed Control
        .speed_is_10_100_0    (1'b0),             // 0 = 1Gbps, 1 = 10/100Mbps
        .speed_is_100_0       (1'b0),             // 0 = 10Mbps, 1 = 100Mbps (when speed_is_10_100=1)

        // Auto-Negotiation
        .an_interrupt_0       (),                 // AN complete interrupt
        .an_adv_config_vector_0(16'd0),           // AN advertisement config
        .an_restart_config_0  (1'b0),             // AN restart

        // Configuration and Status
        .configuration_vector_0(5'b10000),        // [4]=auto_neg_en, [3:1]=reserved, [0]=unidirectional_en
        .status_vector_0      (status_vector),    // Status bits [15:0]

        // Bitslice/RIU Interface (tie off unused)
        .tx_bsc_rst_out       (),
        .rx_bsc_rst_out       (),
        .tx_bs_rst_out        (),
        .rx_bs_rst_out        (),
        .tx_rst_dly_out       (),
        .rx_rst_dly_out       (),
        .tx_bsc_en_vtc_out    (),
        .rx_bsc_en_vtc_out    (),
        .tx_bs_en_vtc_out     (),
        .rx_bs_en_vtc_out     (),
        .riu_clk_out          (),
        .riu_addr_out         (),
        .riu_wr_data_out      (),
        .riu_wr_en_out        (),
        .riu_nibble_sel_out   (),
        .riu_rddata_3         (16'd0),
        .riu_valid_3          (1'b0),
        .riu_prsnt_3          (1'b0),
        .riu_rddata_2         (16'd0),
        .riu_valid_2          (1'b0),
        .riu_prsnt_2          (1'b0),
        .riu_rddata_1         (16'd0),
        .riu_valid_1          (1'b0),
        .riu_prsnt_1          (1'b0),
        .rx_btval_3           (),
        .rx_btval_2           (),
        .rx_btval_1           (),
        .tx_dly_rdy_1         (1'b1),
        .rx_dly_rdy_1         (1'b1),
        .rx_vtc_rdy_1         (1'b1),
        .tx_vtc_rdy_1         (1'b1),
        .tx_dly_rdy_2         (1'b1),
        .rx_dly_rdy_2         (1'b1),
        .rx_vtc_rdy_2         (1'b1),
        .tx_vtc_rdy_2         (1'b1),
        .tx_dly_rdy_3         (1'b1),
        .rx_dly_rdy_3         (1'b1),
        .rx_vtc_rdy_3         (1'b1),
        .tx_vtc_rdy_3         (1'b1),
        .tx_pll_clk_out       (),
        .rx_pll_clk_out       (),
        .tx_rdclk_out         (),

        // System Reset
        .reset                (sys_rst)           // Active-high synchronous reset
    );
`endif


    // ================================================================
    // AXI4-Stream to GMII Converter (TX Path)
    // ================================================================
    // Converts 64-bit AXI4-Stream to 8-bit GMII

    (* mark_debug = "true" *) reg [2:0]  tx_byte_count;
    (* mark_debug = "true" *) reg [63:0] tx_data_reg;
    reg [7:0]  tx_keep_reg;
    reg        tx_last_reg;
    (* mark_debug = "true" *) reg        tx_in_packet;
    (* mark_debug = "true" *) reg        tx_in_preamble;
    reg [2:0]  tx_preamble_count;

    // sys_rst is tied low in EthernetIOBinders; use initial to set Xilinx FF INIT values
    initial begin
        tx_byte_count     = 3'd0;
        tx_data_reg       = 64'd0;
        tx_keep_reg       = 8'd0;
        tx_last_reg       = 1'b0;
        tx_in_packet      = 1'b0;
        tx_in_preamble    = 1'b0;
        tx_preamble_count = 3'd0;
        s_axis_tx_ready   = 1'b1;
    end

    always @(posedge clk125 or posedge sys_rst) begin
        if (sys_rst) begin
            tx_byte_count     <= 3'd0;
            tx_data_reg       <= 64'd0;
            tx_keep_reg       <= 8'd0;
            tx_last_reg       <= 1'b0;
            tx_in_packet      <= 1'b0;
            tx_in_preamble    <= 1'b0;
            tx_preamble_count <= 3'd0;
            s_axis_tx_ready   <= 1'b1;
        end else begin
            if (s_axis_tx_valid && s_axis_tx_ready && !tx_in_packet && !tx_in_preamble) begin
                // First AXI word of a new frame.
                // icenet NET_IP_ALIGN=2: bytes [15:0] are zero-padding — skip them
                // by pre-shifting right 2 bytes so DST_MAC[0] lands at tx_data_reg[7:0].
                tx_data_reg       <= {16'h0000, s_axis_tx_data[63:16]};
                tx_keep_reg       <= {2'b00, s_axis_tx_keep[7:2]};
                tx_last_reg       <= s_axis_tx_last;
                tx_byte_count     <= 3'd2;   // 6 valid bytes remain (positions 2..7)
                tx_in_preamble    <= 1'b1;
                tx_preamble_count <= 3'd0;
                s_axis_tx_ready   <= 1'b0;
            end else if (tx_in_preamble) begin
                // Send preamble (7x 0x55) then SFD (0xD5)
                if (tx_preamble_count < 3'd7) begin
                    tx_preamble_count <= tx_preamble_count + 3'd1;
                end else begin
                    // SFD byte is on gmii_txd this cycle; transition to data next cycle
                    tx_in_preamble <= 1'b0;
                    tx_in_packet   <= 1'b1;
                end
            end else if (tx_in_packet) begin
                // Shift out one frame byte at a time
                if (tx_byte_count < 3'd7) begin
                    tx_data_reg   <= {8'd0, tx_data_reg[63:8]};
                    tx_keep_reg   <= {1'b0, tx_keep_reg[7:1]};
                    tx_byte_count <= tx_byte_count + 3'd1;
                    // Pre-assert ready one cycle before the last byte so the
                    // next AXI word is available exactly when we need to switch.
                    if (tx_byte_count == 3'd6 && !tx_last_reg)
                        s_axis_tx_ready <= 1'b1;
                end else begin
                    // tx_byte_count == 7: last byte of this word is on gmii_txd
                    if (tx_last_reg) begin
                        // End of frame
                        tx_in_packet    <= 1'b0;
                        s_axis_tx_ready <= 1'b1;
                    end else if (s_axis_tx_valid) begin
                        // Seamlessly load next word — no new preamble for continuation words
                        tx_data_reg     <= s_axis_tx_data;
                        tx_keep_reg     <= s_axis_tx_keep;
                        tx_last_reg     <= s_axis_tx_last;
                        tx_byte_count   <= 3'd0;
                        s_axis_tx_ready <= 1'b0;
                        // tx_in_packet stays 1'b1
                    end else begin
                        // Underflow: next word not ready — terminate frame
                        tx_in_packet    <= 1'b0;
                        s_axis_tx_ready <= 1'b1;
                    end
                end
            end
        end
    end

    // Preamble bytes (0x55) and SFD (0xD5) precede each frame on the GMII bus
    assign gmii_txd  = tx_in_preamble ? ((tx_preamble_count == 3'd7) ? 8'hD5 : 8'h55)
                                      : tx_data_reg[7:0];
    assign gmii_tx_en = tx_in_preamble || (tx_in_packet && tx_keep_reg[0]);
    assign gmii_tx_er = 1'b0;

    // ================================================================
    // GMII to AXI4-Stream Converter (RX Path)
    // ================================================================
    // Converts 8-bit GMII to 64-bit AXI4-Stream

    (* mark_debug = "true" *) reg [2:0]  rx_byte_count;
    (* mark_debug = "true" *) reg [63:0] rx_data_accum;
    reg [7:0]  rx_keep_accum;
    (* mark_debug = "true" *) reg        rx_in_packet;
    (* mark_debug = "true" *) reg        rx_sfd_seen;

    // Added 20260702: detects a fresh preamble+SFD pattern appearing WHILE
    // rx_in_packet is still set. The original design assumed the PCS core
    // always deasserts gmii_rx_dv between frames (clean IFG), and only used
    // that deassertion to close out a frame. Under back-to-back traffic the
    // PCS core can keep gmii_rx_dv asserted across the frame boundary, so
    // the next frame's own preamble/SFD gets silently swallowed as payload
    // of the previous frame — this is the corruption seen in captured RX
    // dumps (0x55 runs / 0xd5 appearing mid-frame). rx_preamble_run tracks
    // a rolling count of consecutive 0x55 bytes on gmii_rxd regardless of
    // FSM state; combined with the current byte being 0xD5, this fires a
    // forced resync even when the FSM still thinks it's mid-packet.
    (* mark_debug = "true" *) reg [2:0]  rx_preamble_run;
    (* mark_debug = "true" *) reg        rx_midstream_resync;  // debug/stat only

    wire rx_fresh_sfd = gmii_rx_dv && (rx_preamble_run == 3'd6) && (gmii_rxd == 8'hD5);

    always @(posedge clk125 or posedge sys_rst) begin
        if (sys_rst) begin
            rx_preamble_run <= 3'd0;
        end else if (gmii_rx_dv) begin
            if (gmii_rxd == 8'h55)
                rx_preamble_run <= (rx_preamble_run == 3'd6) ? 3'd6 : rx_preamble_run + 3'd1;
            else
                rx_preamble_run <= 3'd0;
        end else begin
            rx_preamble_run <= 3'd0;
        end
    end

    always @(posedge clk125 or posedge sys_rst) begin
        if (sys_rst) begin
            rx_byte_count       <= 3'd0;
            rx_data_accum       <= 64'd0;
            rx_keep_accum       <= 8'd0;
            rx_in_packet        <= 1'b0;
            rx_sfd_seen         <= 1'b0;
            rx_midstream_resync <= 1'b0;
            m_axis_rx_valid     <= 1'b0;
            m_axis_rx_data      <= 64'd0;
            m_axis_rx_keep      <= 8'd0;
            m_axis_rx_last      <= 1'b0;
        end else begin
            rx_midstream_resync <= 1'b0;
            if (rx_in_packet && rx_sfd_seen && rx_fresh_sfd) begin
                // A new frame's SFD showed up before the previous one was
                // ever closed out (no IFG seen). Force-close the previous
                // frame right now (whatever is in the accumulator — the
                // trailing bytes are already tainted by the incoming
                // preamble and cannot be un-shifted, but this stops the
                // corruption from spreading into the NEW frame too) and
                // immediately restart capture exactly as if this were a
                // normal SFD detection.
                rx_midstream_resync <= 1'b1;
                m_axis_rx_valid <= 1'b1;
                m_axis_rx_data  <= rx_data_accum;
                m_axis_rx_keep  <= rx_keep_accum;
                m_axis_rx_last  <= 1'b1;
                rx_sfd_seen     <= 1'b1;
                rx_in_packet    <= 1'b1;
                rx_byte_count   <= 3'd2;
                rx_data_accum   <= 64'd0;
                rx_keep_accum   <= 8'd0;
            end else if (gmii_rx_dv) begin
                if (!rx_sfd_seen) begin
                    // First byte with rx_dv=1 is SFD (0xD5) — discard it
                    // Start at byte_count=2 so the low 2 bytes of the first
                    // output word stay zero — this is the NET_IP_ALIGN=2
                    // padding the icenet driver expects before the Ethernet frame.
		    if(gmii_rxd == 8'hD5) begin
                        rx_sfd_seen     <= 1'b1;
                        rx_in_packet    <= 1'b1;
                        m_axis_rx_valid <= 1'b0;
                        rx_byte_count   <= 3'd2;
                    end
                end else begin
                    // Accumulate frame bytes (dest MAC onwards)
                    rx_data_accum <= {gmii_rxd, rx_data_accum[63:8]};
                    rx_keep_accum <= {1'b1, rx_keep_accum[7:1]};
                    rx_byte_count <= rx_byte_count + 3'd1;

                    if (rx_byte_count == 3'd7) begin
                        m_axis_rx_valid <= 1'b1;
                        m_axis_rx_data  <= {gmii_rxd, rx_data_accum[63:8]};
                        m_axis_rx_keep  <= 8'hFF;
                        m_axis_rx_last  <= 1'b0;
                        rx_byte_count   <= 3'd0;
                        rx_data_accum   <= 64'd0;
                        rx_keep_accum   <= 8'd0;
                    end else begin
                        m_axis_rx_valid <= 1'b0;
                    end
                end
            end else if (rx_in_packet) begin
                // End of packet - output remaining bytes
                m_axis_rx_valid <= 1'b1;
                m_axis_rx_data  <= rx_data_accum;
                m_axis_rx_keep  <= rx_keep_accum;
                m_axis_rx_last  <= 1'b1;
                rx_in_packet    <= 1'b0;
                rx_sfd_seen     <= 1'b0;
                rx_byte_count   <= 3'd0;
                rx_data_accum   <= 64'd0;
                rx_keep_accum   <= 8'd0;
            end else begin
                m_axis_rx_valid <= 1'b0;
                rx_sfd_seen     <= 1'b0;
            end
        end
    end

    // ================================================================
    // Status Signals
    // ================================================================
    assign link_up = status_vector[0];
    assign speed_is_100 = status_vector[10];
    assign speed_is_10_100 = status_vector[11];

    // Temporary assignments for simulation without IP
/*    assign sgmii_txp = 1'b0;
    assign sgmii_txn = 1'b1;
    assign link_up = 1'b1;
    assign speed_is_100 = 1'b0;
    assign speed_is_10_100 = 1'b0;
    */

endmodule
