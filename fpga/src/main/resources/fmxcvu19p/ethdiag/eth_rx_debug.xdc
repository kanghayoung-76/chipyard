# =============================================================================
# eth_rx_debug.xdc -- DIAGNOSTIC ONLY. Remove when done.
#
# Probes the conditions that gate "RX FIFO -> AXI DMA" transfer. Net names
# verified against the generated BD netlist (vela_eth_dma.v).
#
# ALL of these are in the axi_aclk domain -> ONE ILA core. The 125 MHz
# PCS/GMII stage is deliberately not probed: rx_statistics_vector already
# proved the MAC receives good frames (0x8001491 -> rx_good_frame=1,
# rx_address_match=1, no FCS/alignment errors).
#
# Flow: add before synthesis, then Tools > Set Up Debug (or create_debug_core).
# Suggested trigger:  TVALID == 1 && TREADY == 0   (position 25%)
# Better: arm with eth0 DOWN, trigger on TVALID rising, then `ip link set eth0 up`
# so you capture the very first frame instead of the jammed steady state.
# =============================================================================

proc _dbg {pat} {
  set nets [get_nets -hier -filter "NAME =~ \"$pat\""]
  if {[llength $nets] == 0} {
    puts "WARNING: debug pattern matched nothing: $pat"
  } else {
    set_property MARK_DEBUG true $nets
  }
}

# ---- condition A : the AXI-Stream handshake --------------------------------
# A transfer happens only on a cycle where TVALID and TREADY are both high.
_dbg "*axi_ethernet_0_m_axis_rxd_TVALID*"
_dbg "*axi_ethernet_0_m_axis_rxd_TREADY*"
_dbg "*axi_ethernet_0_m_axis_rxd_TLAST*"
_dbg "*axi_ethernet_0_m_axis_rxd_TKEEP*"
_dbg "*axi_aresetn*"

# ---- condition C.3 : has S2MM fetched a descriptor? ------------------------
# If no SG read completes before the first TVALID, the engine has no
# descriptor loaded and cannot accept the frame. ARADDR shows WHICH descriptor
# (expect the S2MM ring base, e.g. 0x1_27800000).
_dbg "*axi_dma_0_M_AXI_SG_ARVALID*"
_dbg "*axi_dma_0_M_AXI_SG_ARREADY*"
_dbg "*axi_dma_0_M_AXI_SG_ARADDR*"
_dbg "*axi_dma_0_M_AXI_SG_RVALID*"
_dbg "*axi_dma_0_M_AXI_SG_RLAST*"

# ---- condition C.7 : can S2MM drain to memory? -----------------------------
# AWADDR is the RX buffer address -- watch its low bits for the alignment that
# caused DMAIntErr (2-byte aligned, e.g. 0x1_0228e242). BRESP != 0 means the
# write was rejected downstream.
_dbg "*axi_dma_0_M_AXI_S2MM_AWVALID*"
_dbg "*axi_dma_0_M_AXI_S2MM_AWREADY*"
_dbg "*axi_dma_0_M_AXI_S2MM_AWADDR*"
_dbg "*axi_dma_0_M_AXI_S2MM_AWLEN*"
_dbg "*axi_dma_0_M_AXI_S2MM_WVALID*"
_dbg "*axi_dma_0_M_AXI_S2MM_WREADY*"
_dbg "*axi_dma_0_M_AXI_S2MM_WLAST*"
_dbg "*axi_dma_0_M_AXI_S2MM_BVALID*"
_dbg "*axi_dma_0_M_AXI_S2MM_BRESP*"

# ---- condition D : frame completion ----------------------------------------
# The status word carries the length into app4; without it no descriptor
# retires even if all the data transferred.
_dbg "*axi_ethernet_0_m_axis_rxs_TVALID*"
_dbg "*axi_ethernet_0_m_axis_rxs_TREADY*"
_dbg "*axi_ethernet_0_m_axis_rxs_TLAST*"
_dbg "*axi_dma_0_s2mm_introut*"
