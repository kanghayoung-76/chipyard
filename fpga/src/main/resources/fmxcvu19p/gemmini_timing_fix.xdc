# =============================================================================
# Gemmini Timing Fix Constraints for VelaRocketHugeGemminiNormal
# XCVU19P FMXCVU19P Board
# Updated with correct Chipyard module naming conventions
# =============================================================================

# -----------------------------------------------------------------------------
# Asynchronous Clock Groups (Primary Fix)
# -----------------------------------------------------------------------------
# Ensure DDR UI clock and System clock are properly marked as asynchronous
set_clock_groups -asynchronous \
    -group [get_clocks -of_objects [get_pins -hierarchical -filter {NAME =~ *c0_ddr4_ui_clk}] -quiet] \
    -group [get_clocks -of_objects [get_pins -hierarchical -filter {NAME =~ *harnessSysPLL*clk_out*}] -quiet] \
    -group [get_clocks JTCK -quiet]

# -----------------------------------------------------------------------------
# AXI4 Async Crossing (System <-> DDR MIG)
# These are the main CDC paths between system clock and DDR UI clock
# -----------------------------------------------------------------------------
# AXI4AsyncCrossingSink - receives from system clock, outputs to DDR clock
set_false_path -to [get_cells -hierarchical -filter {REF_NAME =~ AXI4AsyncCrossingSink*} -quiet]
set_false_path -from [get_cells -hierarchical -filter {REF_NAME =~ AXI4AsyncCrossingSink*} -quiet]

# AXI4AsyncCrossingSource - receives from DDR clock, outputs to system clock
set_false_path -to [get_cells -hierarchical -filter {REF_NAME =~ AXI4AsyncCrossingSource*} -quiet]
set_false_path -from [get_cells -hierarchical -filter {REF_NAME =~ AXI4AsyncCrossingSource*} -quiet]

# -----------------------------------------------------------------------------
# AsyncQueue CDC Paths
# -----------------------------------------------------------------------------
# AsyncQueueSink modules
set_false_path -to [get_cells -hierarchical -filter {REF_NAME =~ AsyncQueueSink*} -quiet]
set_false_path -from [get_cells -hierarchical -filter {REF_NAME =~ AsyncQueueSink*} -quiet]

# AsyncQueueSource modules
set_false_path -to [get_cells -hierarchical -filter {REF_NAME =~ AsyncQueueSource*} -quiet]
set_false_path -from [get_cells -hierarchical -filter {REF_NAME =~ AsyncQueueSource*} -quiet]

# AsyncValidSync - valid signal synchronizer inside AsyncQueue
set_false_path -to [get_cells -hierarchical -filter {REF_NAME == AsyncValidSync} -quiet]
set_false_path -from [get_cells -hierarchical -filter {REF_NAME == AsyncValidSync} -quiet]

# -----------------------------------------------------------------------------
# Synchronizer Shift Registers
# -----------------------------------------------------------------------------
# AsyncResetSynchronizerShiftReg - multi-stage synchronizer
set_false_path -to [get_cells -hierarchical -filter {REF_NAME =~ AsyncResetSynchronizerShiftReg*} -quiet]

# AsyncResetSynchronizerPrimitiveShiftReg
set_false_path -to [get_cells -hierarchical -filter {REF_NAME =~ AsyncResetSynchronizerPrimitiveShiftReg*} -quiet]

# -----------------------------------------------------------------------------
# Clock Crossing Registers
# -----------------------------------------------------------------------------
# ClockCrossingReg - data register for clock domain crossing
set_false_path -to [get_cells -hierarchical -filter {REF_NAME =~ ClockCrossingReg*} -quiet]
set_false_path -from [get_cells -hierarchical -filter {REF_NAME =~ ClockCrossingReg*} -quiet]

# -----------------------------------------------------------------------------
# Reset Synchronizers
# -----------------------------------------------------------------------------
# ClockGroupResetSynchronizer
set_false_path -to [get_cells -hierarchical -filter {REF_NAME == ClockGroupResetSynchronizer} -quiet]
set_false_path -from [get_cells -hierarchical -filter {REF_NAME == ClockGroupResetSynchronizer} -quiet]

# ResetCatchAndSync
set_false_path -to [get_cells -hierarchical -filter {REF_NAME =~ ResetCatchAndSync*} -quiet]
set_false_path -from [get_cells -hierarchical -filter {REF_NAME =~ ResetCatchAndSync*} -quiet]

# AsyncResetReg / AsyncResetRegVec
set_false_path -to [get_cells -hierarchical -filter {REF_NAME =~ AsyncResetReg*} -quiet]
set_false_path -from [get_cells -hierarchical -filter {REF_NAME =~ AsyncResetReg*} -quiet]

# -----------------------------------------------------------------------------
# Alternative: Use NAME pattern if REF_NAME doesn't work
# (Vivado sometimes uses different naming after synthesis)
# -----------------------------------------------------------------------------
# AsyncQueue by instance name
set_false_path -through [get_pins -hierarchical -filter {NAME =~ */AsyncQueueSink*/io_async_*} -quiet]
set_false_path -through [get_pins -hierarchical -filter {NAME =~ */AsyncQueueSource*/io_async_*} -quiet]

# Gray-coded index synchronizers (ridx/widx)
set_false_path -through [get_pins -hierarchical -filter {NAME =~ *widx_widx_gray*/io_d} -quiet]
set_false_path -through [get_pins -hierarchical -filter {NAME =~ *ridx_ridx_gray*/io_d} -quiet]

# Synchronizer chain input pins
set_false_path -to [get_pins -hierarchical -filter {NAME =~ *sync_0*/D} -quiet]
set_false_path -to [get_pins -hierarchical -filter {NAME =~ *sync_1*/D} -quiet]
set_false_path -to [get_pins -hierarchical -filter {NAME =~ *sync_2*/D} -quiet]

# -----------------------------------------------------------------------------
# Power-On Reset and System Reset
# -----------------------------------------------------------------------------
set_false_path -from [get_ports reset -quiet]
set_false_path -through [get_nets -hierarchical -filter {NAME =~ *powerOnReset*} -quiet]
set_false_path -through [get_nets -hierarchical -filter {NAME =~ *power_on_reset*} -quiet]

# -----------------------------------------------------------------------------
# JTAG Clock Domain
# -----------------------------------------------------------------------------
set_false_path -from [get_ports jtag_jtag_TCK -quiet]
set_false_path -from [get_ports jtag_jtag_TMS -quiet]
set_false_path -from [get_ports jtag_jtag_TDI -quiet]
set_false_path -to [get_ports jtag_jtag_TDO -quiet]

# -----------------------------------------------------------------------------
# MIG Internal Paths (handled by MIG IP but add explicit constraints)
# -----------------------------------------------------------------------------
set_false_path -from [get_cells -hierarchical -filter {NAME =~ *mig*rst_sync*} -quiet]
set_false_path -to [get_cells -hierarchical -filter {NAME =~ *mig*rst_sync*} -quiet]

# =============================================================================
# VIVADO TIMING OPTIMIZATION DIRECTIVES (Alternative to RTL changes)
# =============================================================================
# These directives help Vivado optimize the deep combinational paths in Gemmini
# without requiring RTL modifications.

# -----------------------------------------------------------------------------
# 1. Enable Retiming (Register Rebalancing) - CRITICAL FOR TIMING CLOSURE
# -----------------------------------------------------------------------------
# Allows Vivado to move registers across combinational logic to balance timing
# Apply to Gemmini accumulator scale unit and all related modules

# Target the specific acc_scale_unit with the 55-level combinational path
set_property REGISTER_BALANCING YES [get_cells -hierarchical -filter {NAME =~ *acc_scale_unit*} -quiet]
set_property REGISTER_BALANCING YES [get_cells -hierarchical -filter {NAME =~ *gemmini*spad*} -quiet]

# Target the floating-point modules inside scale_func
set_property REGISTER_BALANCING YES [get_cells -hierarchical -filter {NAME =~ *scaled_data_s2*} -quiet]
set_property REGISTER_BALANCING YES [get_cells -hierarchical -filter {NAME =~ *in_to_rec_fn*} -quiet]
set_property REGISTER_BALANCING YES [get_cells -hierarchical -filter {NAME =~ *muladder*} -quiet]
set_property REGISTER_BALANCING YES [get_cells -hierarchical -filter {NAME =~ *rec_fn_to_in*} -quiet]
set_property REGISTER_BALANCING YES [get_cells -hierarchical -filter {NAME =~ *MulAddRecFN*} -quiet]
set_property REGISTER_BALANCING YES [get_cells -hierarchical -filter {NAME =~ *INToRecFN*} -quiet]
set_property REGISTER_BALANCING YES [get_cells -hierarchical -filter {NAME =~ *RecFNToIN*} -quiet]

# -----------------------------------------------------------------------------
# 2. Retiming-Friendly Register Properties
# -----------------------------------------------------------------------------
# Allow forward and backward retiming on pipeline registers
set_property REGISTER_BALANCING FORWARD [get_cells -hierarchical -filter {NAME =~ *stage1_out_data*} -quiet]
set_property REGISTER_BALANCING BACKWARD [get_cells -hierarchical -filter {NAME =~ *stage3_out_data*} -quiet]

# Mark ShiftRegister chains as retimable
set_property REGISTER_BALANCING YES [get_cells -hierarchical -filter {NAME =~ *ShiftRegister*} -quiet]

# -----------------------------------------------------------------------------
# 3. Performance-Optimized Synthesis Attributes
# -----------------------------------------------------------------------------
# Enable resource sharing optimization
set_property RESOURCE_SHARING ON [get_cells -hierarchical -filter {NAME =~ *gemmini*} -quiet]

# Flatten hierarchy in acc_scale_unit to allow better optimization
set_property KEEP_HIERARCHY NO [get_cells -hierarchical -filter {NAME =~ *acc_scale_unit*} -quiet]

# -----------------------------------------------------------------------------
# 4. Physical Optimization Hints
# -----------------------------------------------------------------------------
# Mark critical path cells for priority placement
set_property PHYS_OPT_RETIMED_INPUTS YES [get_cells -hierarchical -filter {NAME =~ *acc_scale_unit*} -quiet]

# -----------------------------------------------------------------------------
# 5. DSP Pipelining (for MulAddRecFN)
# -----------------------------------------------------------------------------
# Enable DSP pipelining for better timing
set_property USE_DSP48 YES [get_cells -hierarchical -filter {NAME =~ *muladder*} -quiet]
set_property DSP_PIPELINE 3 [get_cells -hierarchical -filter {NAME =~ *DSP48E2*} -quiet] 2>/dev/null

# =============================================================================
# VIVADO TCL COMMANDS FOR MANUAL OPTIMIZATION
# =============================================================================
# Run these commands in Vivado TCL console for additional timing closure:
#
# 1. Enable aggressive physical optimization:
#    set_property STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]
#
# 2. Enable post-route physical optimization:
#    set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]
#    set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]
#
# 3. Use Performance_ExplorePostRoutePhysOpt strategy:
#    set_property strategy Performance_ExplorePostRoutePhysOpt [get_runs impl_1]
#
# 4. Enable retiming in synthesis:
#    set_property STEPS.SYNTH_DESIGN.ARGS.RETIMING true [get_runs synth_1]
#
# 5. Check current timing:
#    report_timing_summary -delay_type min_max -report_unconstrained -max_paths 10
#
# 6. Analyze critical path:
#    report_timing -from [get_cells -hierarchical -filter {NAME =~ *gemmini*spad*norm_unit*}] \
#                  -to [get_cells -hierarchical -filter {NAME =~ *gemmini*spad*acc_scale*}] \
#                  -max_paths 10 -nworst 5

# =============================================================================
# Debug Commands (Run in Vivado TCL console to diagnose timing issues)
# =============================================================================
# report_clock_interaction -delay_type min_max -file clock_interaction.rpt
# report_timing -from [all_clocks] -to [all_clocks] -max_paths 50 -slack_lesser_than 0 -file failing_paths.rpt
# report_timing_summary -file timing_summary.rpt
# get_cells -hierarchical -filter {REF_NAME =~ AsyncQueue*}
# get_cells -hierarchical -filter {REF_NAME =~ *Crossing*}
