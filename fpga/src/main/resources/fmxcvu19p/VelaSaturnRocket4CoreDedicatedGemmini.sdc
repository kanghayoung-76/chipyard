# =============================================================================
# Project-specific constraints for VelaSaturnRocket4CoreDedicatedGemmini
# XCVU19P FMXCVU19P Board
# 4x RocketCore + 4x Saturn (vLen=256, dLen=128) + 4x Gemmini
#
# NOTE: fmxcvu19p-additional.sdc already runs create_pblock / add_cells_to_pblock
# for pblock_gemmini0-7.  This file only resizes those pblocks and fully
# manages the Saturn pblocks (which are commented out in the shared file).
# =============================================================================

# -----------------------------------------------------------------------------
# Multicycle path: Gemmini ld_weights DRAM address calculation
# Path: ld_weights/_dram_offset_T_19 DSP cascade → command_p/stages_0
# Depth: 28 logic levels (CARRY8×4 + DSP cascade×5) → ~10.4ns, needs 2 cycles
# Same pipeline pattern as the existing LoopConv command_p constraint
# -----------------------------------------------------------------------------
set_multicycle_path -setup 2 \
  -from [get_cells -hierarchical -filter {NAME =~ *ld_weights*_dram_offset*DSP*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *ld_weights*command_p*stages_0*reg*}]
set_multicycle_path -hold  1 \
  -from [get_cells -hierarchical -filter {NAME =~ *ld_weights*_dram_offset*DSP*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *ld_weights*command_p*stages_0*reg*}]

# -----------------------------------------------------------------------------
# Multicycle path: Saturn vxufp_int FP multiply unit
# Path: pipe_sels/pipe_valids → fus_*/mul_out_pipe
# Depth: 26-27 logic levels (CARRY8×6-7 + DSP cascade×4) → ~11-17ns, needs 2 cycles
# Note: source can be pipe_sels_0_reg or pipe_valids_0_reg depending on path
# -----------------------------------------------------------------------------
set_multicycle_path -setup 2 \
  -from [get_cells -hierarchical -filter {NAME =~ *vxufp_int*pipe_*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vxufp_int*fus_*/mul_out_pipe*reg*}]
set_multicycle_path -hold  1 \
  -from [get_cells -hierarchical -filter {NAME =~ *vxufp_int*pipe_*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vxufp_int*fus_*/mul_out_pipe*reg*}]

# -----------------------------------------------------------------------------
# Multicycle path: Saturn vmu store pointer → vmu_index_q register enables
# Path: vmu/siq_sas_ptr_reg → vu/vmu_index_q/regs_*/CE
# Depth: 31 logic levels, route-dominant (6.5ns route vs 4.0ns logic)
# siq_sas_ptr is a store instruction queue pointer — queue update takes 2 cycles
# -----------------------------------------------------------------------------
set_multicycle_path -setup 2 \
  -from [get_cells -hierarchical -filter {NAME =~ *vmu*siq_sas_ptr*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vmu_index_q*regs*reg*}]
set_multicycle_path -hold  1 \
  -from [get_cells -hierarchical -filter {NAME =~ *vmu*siq_sas_ptr*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vmu_index_q*regs*reg*}]

# -----------------------------------------------------------------------------
# PBlock constraints for 4-core layout
# Device: XCVU19P — clock regions X0Y0 to X7Y14
# Clock root typically at X4Y7
#
# Layout (no overlaps, Y11+ reserved for MIG):
#   Y8-Y10: gemmini2 (X0-X3)   | gemmini1 (X4-X7)   [3 rows each]
#   Y6-Y7:  saturn2  (X0-X3)   | saturn1  (X4-X7)   [2 rows, near clock root]
#   Y2-Y5:  gemmini0 (X0-X3)   | gemmini3 (X4-X7)   [4 rows each]
#   Y0-Y1:  saturn0  (X0-X3)   | saturn3  (X4-X7)   [2 rows each]
#
# gemmini pblocks are created and cells assigned by fmxcvu19p-additional.sdc.
# Only resize and routing containment are set here.
# -----------------------------------------------------------------------------

resize_pblock pblock_gemmini0 -add {CLOCKREGION_X0Y5:CLOCKREGION_X3Y2}
resize_pblock pblock_gemmini1 -add {CLOCKREGION_X4Y10:CLOCKREGION_X7Y8}
resize_pblock pblock_gemmini2 -add {CLOCKREGION_X0Y10:CLOCKREGION_X3Y8}
resize_pblock pblock_gemmini3 -add {CLOCKREGION_X4Y5:CLOCKREGION_X7Y2}

set_property CONTAIN_ROUTING false [get_pblocks pblock_gemmini0]
set_property CONTAIN_ROUTING false [get_pblocks pblock_gemmini1]
set_property CONTAIN_ROUTING false [get_pblocks pblock_gemmini2]
set_property CONTAIN_ROUTING false [get_pblocks pblock_gemmini3]

# Saturn pblocks (adjacent to paired gemmini, no overlap)
create_pblock pblock_saturn0
create_pblock pblock_saturn1
create_pblock pblock_saturn2
create_pblock pblock_saturn3

add_cells_to_pblock [get_pblocks pblock_saturn0] \
  [get_cells -quiet [list chiptop0/system/tile_prci_domain/element_reset_domain_rockettile/vector_unit]]
add_cells_to_pblock [get_pblocks pblock_saturn1] \
  [get_cells -quiet [list chiptop0/system/tile_prci_domain_1/element_reset_domain_rockettile/vector_unit]]
add_cells_to_pblock [get_pblocks pblock_saturn2] \
  [get_cells -quiet [list chiptop0/system/tile_prci_domain_2/element_reset_domain_rockettile/vector_unit]]
add_cells_to_pblock [get_pblocks pblock_saturn3] \
  [get_cells -quiet [list chiptop0/system/tile_prci_domain_3/element_reset_domain_rockettile/vector_unit]]

resize_pblock pblock_saturn0 -add {CLOCKREGION_X0Y1:CLOCKREGION_X3Y0}
resize_pblock pblock_saturn1 -add {CLOCKREGION_X4Y7:CLOCKREGION_X7Y6}
resize_pblock pblock_saturn2 -add {CLOCKREGION_X0Y7:CLOCKREGION_X3Y6}
resize_pblock pblock_saturn3 -add {CLOCKREGION_X4Y1:CLOCKREGION_X7Y0}

set_property CONTAIN_ROUTING false [get_pblocks pblock_saturn0]
set_property CONTAIN_ROUTING false [get_pblocks pblock_saturn1]
set_property CONTAIN_ROUTING false [get_pblocks pblock_saturn2]
set_property CONTAIN_ROUTING false [get_pblocks pblock_saturn3]
