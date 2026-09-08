package chipyard.fpga.vcu118

import chisel3._
import org.chipsalliance.cde.config.{Config, Parameters}
import freechips.rocketchip.diplomacy.LazyModule
import freechips.rocketchip.tile.BuildRoCC

// GemminiNPU
import velaNPU._
import velaNPU.Arithmetic.SIntArithmetic
import hardfloat._

/**
 * FPGA-optimized Gemmini configurations for XCVU19P
 *
 * These configurations increase the accumulator scale latency to improve
 * timing closure on FPGAs. The AccScalePipe module has been modified to
 * distribute pipeline stages across the combinational path instead of
 * placing all registers at the output.
 *
 * === Remaining Synchronization Issue (PE output skew) ===
 *
 * In PE/MpPE, the data and control outputs have a 1-cycle skew at the PE level:
 *   - out_result := ShiftRegister(mul_unit.io.out_result, 1)  [+1 cycle, registered]
 *   - out_valid  := io.in_valid                               [+0 cycles, combinational]
 *   - out_last   := io.in_last                                [+0 cycles, combinational]
 *   - out_id     := io.in_id                                  [+0 cycles, combinational]
 *
 * This skew is internally compensated across two pipeline stages so that
 * both data and valid arrive at MpExeUnit output with exactly +2 cycles of delay:
 *   - Data path:    Buffvector (+1, double-buffer) + PE ShiftRegister (+1)       = +2 cycles
 *   - Control path: Buffvector RegNext (+1)        + Buffadderlight RegNext (+1) = +2 cycles
 *
 * Therefore mesh_output_delay must be 2 (not the default of 1) to correctly
 * reflect the actual pipeline depth of MpExeUnit.
 *
 * === Additional Issue: out_is_mpgemm skew ===
 *
 * In MpExeUnit, out_is_mpgemm is delayed by 3 cycles:
 *   io.out_is_mpgemm := ShiftRegister(io.in_is_mpgemm, 3)
 *
 * But out_c / out_valid / out_last / out_id are only delayed by 2 cycles.
 * This causes a 1-cycle misalignment: when resp.valid fires for input at cycle T,
 * resp.bits.is_mpgemm reflects in_is_mpgemm from cycle T-3 (not T-2).
 * This cannot be corrected via config alone; MpExeUnit.scala must be fixed
 * to use ShiftRegister(io.in_is_mpgemm, 2) instead of 3.
 */
object FPGAGemminiConfigs {

  // FPGA-optimized config with increased scale latency for timing closure
  // Original defaultConfig has latency=8, this increases to 12 for FPGA
  // Also enables distributed pipeline to break up the deep combinational path
  val fpgaConfig = GemminiConfigs.defaultConfig.copy(
    // Enable distributed pipeline for FPGA timing closure
    //acc_scale_distribute_pipeline = true,
    // Enable normalizations to use AccScalePipe path (required when num_scale_units > 0)

    // Correct pipeline depth: Buffvector (+1) + PE ShiftRegister (+1) = 2 cycles total.
    // The default value of 1 underestimates this by one stage.
    // NOTE: mesh_output_delay is currently unused in the MpExeUnit active code path;
    // this value documents the intended depth for when the parameter is wired up.
    mesh_output_delay = 2,

    // Increase accumulator scale latency for FPGA timing
    acc_scale_args = Some(ScaleArguments(
      (t: SInt, f: Float) => {
        // =======================================================================
        // FPGA-OPTIMIZED PIPELINED SCALE FUNCTION
        // Adds pipeline registers between floating-point stages to break up
        // the 56-level combinational path into ~15-20 level segments
        // =======================================================================

        val f_rec = recFNFromFN(f.expWidth, f.sigWidth, f.bits)

        // --- STAGE 1: Integer to Recoded Float (~15 logic levels) ---
        val in_to_rec_fn = Module(new INToRecFN(t.getWidth, f.expWidth, f.sigWidth))
        in_to_rec_fn.io.signedIn := true.B
        in_to_rec_fn.io.in := t.asTypeOf(UInt(t.getWidth.W))
        in_to_rec_fn.io.roundingMode := consts.round_near_even
        in_to_rec_fn.io.detectTininess := consts.tininess_afterRounding

        val t_rec = in_to_rec_fn.io.out

        // --- PIPELINE REGISTER 1: After INToRecFN ---
        val t_rec_pipe = RegNext(t_rec)
        val f_rec_pipe = RegNext(f_rec)

        // --- STAGE 2: Multiply-Add (~20 logic levels) ---
        val muladder = Module(new MulAddRecFN(f.expWidth, f.sigWidth))
        muladder.io.op := 0.U
        muladder.io.roundingMode := consts.round_near_even
        muladder.io.detectTininess := consts.tininess_afterRounding

        muladder.io.a := t_rec_pipe
        muladder.io.b := f_rec_pipe
        muladder.io.c := 0.U

        val mul_out = muladder.io.out

        // --- PIPELINE REGISTER 2: After MulAddRecFN ---
        val mul_out_pipe = RegNext(mul_out)

        // --- STAGE 3: Recoded Float to Integer + Saturation (~15 logic levels) ---
        val rec_fn_to_in = Module(new RecFNToIN(f.expWidth, f.sigWidth, t.getWidth))
        rec_fn_to_in.io.in := mul_out_pipe
        rec_fn_to_in.io.roundingMode := consts.round_near_even
        rec_fn_to_in.io.signedOut := true.B

        val overflow = rec_fn_to_in.io.intExceptionFlags(1)
        val maxsat = ((1 << (t.getWidth-1))-1).S
        val minsat = (-(1 << (t.getWidth-1))).S
        val sign = rawFloatFromRecFN(f.expWidth, f.sigWidth, rec_fn_to_in.io.in).sign
        val sat = Mux(sign, minsat, maxsat)

        Mux(overflow, sat, rec_fn_to_in.io.out.asTypeOf(t))
      },
      // Note: 2 extra cycles added inside scale_func (pipeline registers)
      // Total latency = external latency (12) + internal pipeline (2) = 14 cycles
      12,  // External latency for FPGA timing closure
      Float(8, 24),
      -1,
      identity = "1.0",
      c_str = "({float y = ROUND_NEAR_EVEN((x) * (scale)); y > INT8_MAX ? INT8_MAX : (y < INT8_MIN ? INT8_MIN : (acc_t)y);})"
    ))
  )

  // Higher latency config for very tight timing requirements
  val fpgaHighLatencyConfig = fpgaConfig.copy(
    acc_scale_args = Some(fpgaConfig.acc_scale_args.get.copy(latency = 16))
  )
}

/**
 * FPGA-optimized Gemmini Config mixin
 * Use this instead of DefaultGemminiConfig for FPGA builds with timing issues
 */
class FPGAGemminiConfig[T <: Data : Arithmetic, U <: Data, V <: Data](
  gemminiConfig: GemminiArrayConfig[T,U,V] = FPGAGemminiConfigs.fpgaConfig
) extends Config((site, here, up) => {
  case BuildRoCC => up(BuildRoCC) ++ Seq(
    (p: Parameters) => {
      implicit val q = p
      val gemmini = LazyModule(new Gemmini(gemminiConfig))
      gemmini
    }
  )
})

/**
 * High-latency FPGA Gemmini Config for very tight timing
 */
class FPGAGemminiHighLatencyConfig[T <: Data : Arithmetic, U <: Data, V <: Data](
  gemminiConfig: GemminiArrayConfig[T,U,V] = FPGAGemminiConfigs.fpgaHighLatencyConfig
) extends Config((site, here, up) => {
  case BuildRoCC => up(BuildRoCC) ++ Seq(
    (p: Parameters) => {
      implicit val q = p
      val gemmini = LazyModule(new Gemmini(gemminiConfig))
      gemmini
    }
  )
})
