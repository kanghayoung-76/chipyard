# =============================================================================
# Vivado Timing Optimization Script for Gemmini on XCVU19P
# =============================================================================
# Source this file in Vivado TCL console after opening the project:
#   source vivado_timing_optimization.tcl
#
# This script enables aggressive timing optimization settings to help close
# timing on the Gemmini accelerator's deep combinational paths.
# =============================================================================

puts "Applying Gemmini timing optimization settings..."

# -----------------------------------------------------------------------------
# 1. Synthesis Settings - Enable Retiming
# -----------------------------------------------------------------------------
# Retiming moves registers across combinational logic to balance delays
if {[llength [get_runs synth_1]] > 0} {
    # Enable retiming - CRITICAL for breaking up deep combinational paths
    set_property STEPS.SYNTH_DESIGN.ARGS.RETIMING true [get_runs synth_1]

    # Use AlternateRoutability for better timing with complex designs
    set_property STEPS.SYNTH_DESIGN.ARGS.DIRECTIVE AlternateRoutability [get_runs synth_1]

    # Flatten design to allow better cross-module optimization
    set_property STEPS.SYNTH_DESIGN.ARGS.FLATTEN_HIERARCHY rebuilt [get_runs synth_1]

    # Additional synthesis options for timing
    set_property STEPS.SYNTH_DESIGN.ARGS.KEEP_EQUIVALENT_REGISTERS false [get_runs synth_1]
    set_property STEPS.SYNTH_DESIGN.ARGS.NO_LC false [get_runs synth_1]

    puts "  - Enabled synthesis retiming"
    puts "  - Set AlternateRoutability directive"
    puts "  - Enabled hierarchy flattening for better optimization"
}

# -----------------------------------------------------------------------------
# 2. Implementation Settings - Aggressive Physical Optimization
# -----------------------------------------------------------------------------
if {[llength [get_runs impl_1]] > 0} {
    # Use performance exploration strategy
    set_property strategy Performance_ExplorePostRoutePhysOpt [get_runs impl_1]

    # Enable aggressive place and route
    set_property STEPS.OPT_DESIGN.ARGS.DIRECTIVE ExploreWithRemap [get_runs impl_1]
    set_property STEPS.PLACE_DESIGN.ARGS.DIRECTIVE ExtraNetDelay_high [get_runs impl_1]
    set_property STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]
    set_property STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]

    # Enable post-route physical optimization
    set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]
    set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]

    puts "  - Applied Performance_ExplorePostRoutePhysOpt strategy"
    puts "  - Enabled aggressive physical optimization directives"
    puts "  - Enabled post-route physical optimization"
}

# -----------------------------------------------------------------------------
# 3. Additional Optimization - Incremental Compile (if previous run exists)
# -----------------------------------------------------------------------------
# Uncomment to enable incremental compile for faster iterations
# if {[file exists [get_property DIRECTORY [current_run]]/route_design.dcp]} {
#     set_property incremental_checkpoint [get_property DIRECTORY [current_run]]/route_design.dcp [get_runs impl_1]
#     puts "  - Enabled incremental compile"
# }

puts ""
puts "Optimization settings applied. Run implementation with:"
puts "  reset_run impl_1"
puts "  launch_runs impl_1 -to_step write_bitstream -jobs 8"
puts ""
puts "After implementation, check timing with:"
puts "  report_timing_summary -delay_type min_max -report_unconstrained -file timing_summary.rpt"
puts ""

# =============================================================================
# Alternative: Manual Flow Commands
# =============================================================================
# If you want to run steps manually instead of using launch_runs:
#
# opt_design -directive ExploreWithRemap
# place_design -directive ExtraNetDelay_high
# phys_opt_design -directive AggressiveExplore
# route_design -directive AggressiveExplore
# phys_opt_design -directive AggressiveExplore
# write_bitstream -force output.bit
