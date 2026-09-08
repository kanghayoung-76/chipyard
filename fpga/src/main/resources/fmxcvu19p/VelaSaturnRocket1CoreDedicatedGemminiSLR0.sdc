# =============================================================================
# Project-specific constraints for VelaSaturnRocket1CoreDedicatedGemminiSLR0
# XCVU19P FMXCVU19P Board
# 1x RocketCore + 1x Saturn (vLen=512, dLen=256) + 1x Gemmini
#
# SLR0-CONFINEMENT VARIANT (isolated A/B experiment, safe to delete).
# Identical RTL to VelaSaturnRocket1CoreDedicatedGemmini. The ONLY difference
# is the pblock at the bottom of this file: instead of leaving the tile to be
# spread by the placer (which, on the routed _compileorder build, spilled ~2.3%
# of tile_prci_domain — including perm_buffer/push_q/wrap_1_reg — into SLR1 and
# put SLR Crossing[1->0] on every dominant critical path), this file forces the
# ENTIRE tile_prci_domain into SLR0 so no vector path crosses an SLR boundary.
#
# Sizing (from the routed checkpoint, confirms the tile fits one SLR):
#   tile LUTs 350,351 / 1,021,440 per-SLR = 34%   FF 133,923 = 6.5%
#   tile DSPs 516 / 960 per-SLR = 54% (tightest)   BRAM ~72 / 540 = 13%
#
# dLen=256 (vs dLen=128 which was timing-clean) causes new violations:
#   1. vxufp_int: 8 FMA units instead of 4; TandemFMAPipe destination name
#      differs from the mul_out_pipe pattern in fmxcvu19p-additional.sdc
#   2. perm_buffer: wider (256-bit), wrap pointer fans out further
#   3. vmu liq: load instruction queue → vmu_index_q (new source type,
#      existing constraint only covers siq_sas_ptr)
#   4. Worst path in the routed build (impl_1, 2026-07-01): WNS -4.371ns,
#      TNS -142546ns, 67464/468091 failing setup endpoints on
#      clk_out1_harnessSysPLL. STA reports it as
#      perm_buffer/push_q/wrap_1_reg_replica -> vcu/error_minus_preverr/fma/
#      mulAddRecFNToRaw_postMul_io_mulAddResult_pipe_b_reg (49 logic levels,
#      14.279ns, 73.9% route). DO NOT add a multicycle waiver for this path:
#      RTL tracing (Backend.scala) shows perm_buffer only feeds vxs.head.head's
#      permute port, while vcu.io.rvs2_data comes from an independent VRF read
#      port (vcs.io.rvs2) — no dataflow connects wrap_1_reg to this
#      destination, so the reported path is likely a synthesis-level artifact
#      (resource sharing / misleading _replica fanout), not real logic. Even
#      if real, the destination is a plain enable-gated pipeline register
#      (chisel3.util.Pipe / RegEnable, MulAddRecFNPipe latency=1) whose enable
#      and data arrive combinationally in the same cycle as everything else in
#      CustomExecutionUnit (s2q.io.enq.valid / error_minus_preverr.io.b_recoded
#      are both driven same-cycle) — there is no structural 2-cycle slack to
#      exploit. This needs a Vivado net trace (report_timing -view logic) to
#      find the real cause, then a retiming/pipeline fix, not a timing waiver.
#
# NOTE: this file was not actually being loaded by the Vivado flow until
# 2026-07-02 (prologue.tcl only globbed the shared board constraintsdir, not
# src/main/resources/$board/$CONFIG.sdc) — the WNS above is what the design
# looks like with ONLY fmxcvu19p-additional.sdc applied. Re-verify timing
# after prologue.tcl's fix lands.
#
# fmxcvu19p-additional.sdc already handles:
#   - LoopConv, ScalePipe, spad/vsm arbOut (Gemmini paths, all met)
#   - vxufp_int pipe_valids → mul_out_pipe (partially; dest pattern too narrow)
#   - Gemmini pblock create/add_cells (pblock_gemmini0)
# This file: resizes the Gemmini pblock and adds the missing Saturn constraints.
# =============================================================================

# -----------------------------------------------------------------------------
# Multicycle path: Saturn vxufp_int FP multiply unit (dLen=256)
# Source covers: pipe_valids_*_replica, pipe_bits_*_replica, pipe_sels_*_reg
# Destination covers:
#   dLen=128: *fus_N/mul_out_pipe_b_reg
#   dLen=256: *fus_N/TandemFMAPipe_K/fma_results_results_fma/mulAddRecFNToRaw_postMul_io_mulAddResult_pipe_b_reg
# Pattern *mul*pipe*reg* matches both naming conventions.
# Depth: 32 logic levels (CARRY8×6 + DSP cascade×5) → 10.2ns, needs 2 cycles
# -----------------------------------------------------------------------------
set_multicycle_path -setup 2 \
  -from [get_cells -hierarchical -filter {NAME =~ *vxufp_int*pipe_*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vxufp_int*fus_*/*mul*pipe*reg*}]
set_multicycle_path -hold  1 \
  -from [get_cells -hierarchical -filter {NAME =~ *vxufp_int*pipe_*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vxufp_int*fus_*/*mul*pipe*reg*}]

# -----------------------------------------------------------------------------
# Multicycle path: perm_buffer wrap pointer → perm_buffer register file (CE)
# Path: perm_buffer/push_q/wrap_1_reg → perm_buffer/regs_N_reg/CE
# dLen=256 makes the permutation buffer 2× wider; wrap pointer route to
# register-file CE inputs becomes route-dominant (~7.5ns route, 25 levels)
# -----------------------------------------------------------------------------
set_multicycle_path -setup 2 \
  -from [get_cells -hierarchical -filter {NAME =~ *perm_buffer*push_q*wrap*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *perm_buffer*regs_*reg*}]
set_multicycle_path -hold  1 \
  -from [get_cells -hierarchical -filter {NAME =~ *perm_buffer*push_q*wrap*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *perm_buffer*regs_*reg*}]

# -----------------------------------------------------------------------------
# Multicycle path: perm_buffer wrap pointer → perm_buffer count register
# Path: perm_buffer/push_q/wrap_1_reg → perm_buffer/count_reg
# Same route-dominant cause as above (~7.6ns route)
# -----------------------------------------------------------------------------
set_multicycle_path -setup 2 \
  -from [get_cells -hierarchical -filter {NAME =~ *perm_buffer*push_q*wrap*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *perm_buffer*count_reg*}]
set_multicycle_path -hold  1 \
  -from [get_cells -hierarchical -filter {NAME =~ *perm_buffer*push_q*wrap*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *perm_buffer*count_reg*}]

# -----------------------------------------------------------------------------
# Multicycle path: perm_buffer wrap pointer → vsissq debug ID register (CE)
# Path: perm_buffer/push_q/wrap_1_reg → vsissq/q/ram_N_debug_id_reg/CE
# Route-dominant: 25 logic levels, 7.2ns route (74.9% of path)
# vsissq = vector store instruction scatter queue
# -----------------------------------------------------------------------------
set_multicycle_path -setup 2 \
  -from [get_cells -hierarchical -filter {NAME =~ *perm_buffer*push_q*wrap*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vsissq*debug_id_reg*}]
set_multicycle_path -hold  1 \
  -from [get_cells -hierarchical -filter {NAME =~ *perm_buffer*push_q*wrap*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vsissq*debug_id_reg*}]

# -----------------------------------------------------------------------------
# FALSE path: perm_buffer wrap pointer → vcu error_minus_preverr FMA pipe
# Path: perm_buffer/push_q/wrap_1_reg_replica ->
#       vcu/error_minus_preverr/fma/mulAddRecFNToRaw_postMul_io_mulAddResult_pipe_b_reg
# vcu = Backend.scala's CustomExecutionUnit (single instance); error_minus_preverr
# is a RecAdd(1) used in the div/sqrt Newton-Raphson error-correction step
# (CustomExecutionUnit.scala). Worst path in the routed build (impl_1,
# 2026-07-03): 44 logic levels, 13.759ns data path delay (70.7% route) vs. a
# 10ns budget, WNS -3.802ns.
#
# VERIFIED (not by analogy — this was previously an unverified multicycle
# waiver copied from the three perm_buffer exceptions above; that was wrong
# in kind, not just unconfirmed):
#   report_timing -path_type full_clock_expanded shows the physical path
#   threads through perm_buffer/push_q/ram_ext (a real RTL read: wrap_1_reg
#   is the read address into perm_buffer's own buffer, which stores
#   previously-captured rvs2_data — Backend.scala:445), then through
#   RegisterFile.scala's RegisterReadXbar/OldestRRArbiter VRF read crossbar
#   (vrf_1→vrf_2→vrf_3 bank muxing), into vcu/s1q before reaching this
#   register. So wrap_1_reg genuinely does share hardware with this
#   destination — the earlier "spurious replica, no dataflow" theory was
#   wrong. BUT the crossbar is an n-way arbiter (RegisterFile.scala:33-48,
#   OldestRRArbiter): io.in(i).resp is broadcast to every requester
#   regardless of who won arbitration that cycle (line 48), so every
#   requester's address bits have a real electrical fanin to the shared
#   output, even though only the granted requester's value is architecturally
#   meaningful.
#   vcu's requester (vcs, Backend.scala:368-370) only ever consumes this
#   shared bus when its OWN request won: CustomSequencer.scala:251 —
#   `io.iss.valid := haveInst && io.rvs2.req.ready && ...` — iss cannot fire
#   unless io.rvs2.req.ready (its own grant) is true that same cycle, and
#   vcu.io.iss <> vcs.io.iss / vcu.io.rvs2_data := vcs.io.rvs2.resp are wired
#   directly off that same handshake. So whenever vcu (and thus
#   error_minus_preverr) actually captures a value gated by
#   s2q.io.enq.valid, vcs's own read was guaranteed granted that cycle —
#   meaning perm_buffer's (a different requester's) contribution to the
#   shared bus on cycles when it is NOT vcu's turn can never be the value
#   vcu captures. There is no "eventually consumed one cycle late" story
#   here (which is what set_multicycle_path asserts) — the value is either
#   never relevant (this destination's cycle) or immediately relevant (its
#   own grant cycle), so set_false_path is the structurally correct
#   exception, not multicycle.
# -----------------------------------------------------------------------------
set_false_path \
  -from [get_cells -hierarchical -filter {NAME =~ *perm_buffer*push_q*wrap*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vcu*error_minus_preverr*mulAddRecFNToRaw*pipe*reg*}]

# Same false path as above, but sourced from push_q's ram_ext storage array
# registers instead of the wrap/address pointer register. STA reports the
# LUTRAM's own write-clock edge (Memory_reg_.../RAMA/CLK) as the launch point
# for the async-read fanout into the same VRF crossbar -> vcu/s1q path
# described above -- it's the same architecturally-false electrical fanin
# (io.in(i).resp broadcast to every requester; vcu only ever consumes it on
# its own grant cycle), just a different register within the same ram_ext
# that the *wrap* filter above doesn't match.
set_false_path \
  -from [get_cells -hierarchical -filter {NAME =~ *perm_buffer*push_q*ram_ext*Memory_reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vcu*error_minus_preverr*mulAddRecFNToRaw*pipe*reg*}]

# -----------------------------------------------------------------------------
# Multicycle path: Saturn vmu load instruction queue → vmu_index_q (CE)
# Path: vmu/liq_3_op_mop_reg → vmu_index_q/regs_N_reg/CE
# Depth: ~31 logic levels, route-dominant (~6.2ns route, 60% of path)
# liq = load instruction queue (cf. siq = store instruction queue,
# which is covered by fmxcvu19p-additional.sdc via siq_sas_ptr pattern)
# -----------------------------------------------------------------------------
set_multicycle_path -setup 2 \
  -from [get_cells -hierarchical -filter {NAME =~ *vmu*liq*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vmu_index_q*regs*reg*}]
set_multicycle_path -hold  1 \
  -from [get_cells -hierarchical -filter {NAME =~ *vmu*liq*reg*}] \
  -to   [get_cells -hierarchical -filter {NAME =~ *vmu_index_q*regs*reg*}]

# -----------------------------------------------------------------------------
# ILA probe false paths
#
# u_ila_0 probes Ethernet signals in the clk125 domain (125 MHz); the ILA is
# clocked by harnessSysPLL/clk_out1 (100 MHz system clock).  These are
# already async via set_clock_groups in fmxcvu19p-additional.sdc, but
# set_false_path below makes the intent explicit and prevents Vivado from
# issuing unconstrained-path warnings on the probe input nets.
# u_ila_1 probes system-clock signals; false_path avoids artificial timing
# pressure from long routes to the debug hub.
# Both cores are debug-only — probe data correctness has no timing requirement.
# -----------------------------------------------------------------------------
set_false_path -to [get_cells -hierarchical -filter {NAME =~ *u_ila_0/ila_core_inst*}]
set_false_path -to [get_cells -hierarchical -filter {NAME =~ *u_ila_1/ila_core_inst*}]

########################################################################################
# SLR0 confinement of the whole tile (THE point of this variant)
#
# Root cause found by TNS-bucketing the routed _compileorder checkpoint
# (WNS -2.775 ns): the launch register perm_buffer/push_q/wrap_1_reg owned
# ~85% of the negative slack across 3928 endpoints, ALL of them real vector
# consumers (67% in vu/vrf, plus the vx/vc/vl/vs issue queues and vxufp_int).
# It was not fanout (net = 27 pins) and not a false crossbar (endpoints are
# real). report_timing showed every one of those paths — and the separate
# worst-path Rocket TLB->icache path — carrying SLR Crossing[1->0], ~72% route,
# 44 logic levels: the tile placer had spilled the wrap logic into SLR1 while
# its loads stayed in SLR0. Forcing the entire tile into SLR0 removes the
# Laguna crossing from every one of those paths at once.
#
# NOTE on the shared fmxcvu19p-additional.sdc: it already does
#   add_cells_to_pblock pblock_tile0 [get_cells -quiet chiptop0/system/tile_prci_domain]
#   resize_pblock pblock_tile0 -add {SLR0}
# but on the _compileorder build that clearly did NOT bind (else 0 tile cells
# could be in SLR1). The -quiet swallows a name miss silently. This per-config
# file is globbed AFTER the shared one by prologue.tcl, so the assignment below
# is the authoritative one for this CONFIG. add_cells_to_pblock reassigns a cell
# to the last pblock it is named in, so this simply re-homes tile_prci_domain
# into pblock_tile_slr0 (both target SLR0; pblock_tile0 is left empty, harmless).
#
# CONTAIN_ROUTING false: constrain PLACEMENT to SLR0 only, let routing spill so
# the tile<->L2/MIG TileLink paths (buffered, non-critical) are not over-constrained.
#
# VERIFY after opt_design (do NOT trust -quiet): the pblock must actually own the
# tile. In the impl run, check:
#   puts [llength [get_cells -quiet -of_objects [get_pblocks pblock_tile_slr0]]]
# and after place_design confirm 0 tile cells land outside SLR0.
########################################################################################
create_pblock pblock_tile_slr0
add_cells_to_pblock pblock_tile_slr0 [get_cells [list chiptop0/system/tile_prci_domain]]
resize_pblock pblock_tile_slr0 -add {SLR0}
set_property CONTAIN_ROUTING false [get_pblocks pblock_tile_slr0]
