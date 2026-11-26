// See LICENSE for license details.

/**
 * WGDMA -- a minimal WID-marked memcpy DMA engine.
 *
 * Purpose: give WorldGuard a *device* master whose bus requests carry a WID,
 * so a checker filters them exactly as it filters core requests. The engine
 * itself is deliberately simple (one outstanding beat, Get-then-Put); the
 * point of this block is the WID path, not DMA throughput.
 *
 * Two MMIO blocks are created:
 *   - the DMA control registers (WGDMAReg below), at params.ctrlAddress
 *   - a WGMarker, at params.markerAddress, whose `wid` register (offset 0x08)
 *     stamps every A-channel request this engine emits. Writes to it are
 *     gated on the requester's WID matching params.mwid, so only the monitor
 *     world can bind/unbind the DMA.
 */

package worldguard.examples

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config.{Parameters, Field, Config}

import freechips.rocketchip.diplomacy._
import freechips.rocketchip.regmapper._
import freechips.rocketchip.resources.SimpleDevice
import freechips.rocketchip.subsystem._
import freechips.rocketchip.tilelink._

import worldguard._

/** Control-register layout, byte offsets from params.ctrlAddress. */
object WGDMAReg {
  val srcLo  = 0x00 // RW  source address [31:0]
  val srcHi  = 0x04 // RW  source address [63:32]
  val dstLo  = 0x08 // RW  destination address [31:0]
  val dstHi  = 0x0c // RW  destination address [63:32]
  val len    = 0x10 // RW  transfer length in bytes (multiple of beatBytes)
  val ctrl   = 0x14 // W   bit0: write 1 to start; reads back BUSY
  // bit0 BUSY, bit1 DONE, bit2 ERROR(denied by a checker), bit3 CORRUPT,
  // bit4 ADDRERR (src/dst high half does not fit this system's address bus)
  val status = 0x18 // RO
  val bytes  = 0x1c // RO  bytes transferred so far
}

case class WGDMAParams(
  ctrlAddress: BigInt,
  markerAddress: BigInt,
  wid: Int,
  mwid: Int,
  widWidth: Int,
  ctrlSize: BigInt = 4096,
  markerSize: Int = 4096)

case object WGDMAKey extends Field[Option[WGDMAParams]](None)

class WGDMA(params: WGDMAParams, beatBytes: Int)(implicit p: Parameters) extends LazyModule {

  val device = new SimpleDevice("wgdma", Seq("sr,wgdma"))

  val ctrlNode = TLRegisterNode(
    address = Seq(AddressSet(params.ctrlAddress, params.ctrlSize - 1)),
    device = device,
    beatBytes = beatBytes)

  val memNode = TLClientNode(Seq(TLMasterPortParameters.v1(Seq(TLClientParameters(
    name = "wgdma", sourceId = IdRange(0, 1))))))

  lazy val module = new Impl
  class Impl extends LazyModuleImp(this) {
    val (mem, edge) = memNode.out(0)

    val xferBytes = edge.manager.beatBytes
    val lgXfer    = log2Ceil(xferBytes)

    val srcLoReg = RegInit(0.U(32.W))
    val srcHiReg = RegInit(0.U(32.W))
    val dstLoReg = RegInit(0.U(32.W))
    val dstHiReg = RegInit(0.U(32.W))
    val lenReg   = RegInit(0.U(32.W))

    val offReg     = RegInit(0.U(32.W))
    val dataReg    = Reg(UInt((xferBytes * 8).W))
    val doneReg    = RegInit(false.B)
    val errReg     = RegInit(false.B)
    val corruptReg = RegInit(false.B)
    val addrErrReg = RegInit(false.B)

    val s_idle :: s_rd_req :: s_rd_resp :: s_wr_req :: s_wr_resp :: Nil = Enum(5)
    val state = RegInit(s_idle)
    val busy  = state =/= s_idle

    // This system's TL address bus is narrower than 64 bits (32 on VCU118,
    // which still covers DDR at 0x8000_0000..0xFFFF_FFFF). Truncate explicitly
    // rather than letting Cat() be silently clipped, and report a non-zero high
    // half that cannot be represented as an ADDRERR rather than quietly
    // transferring from the wrong address.
    val addrBits = edge.bundle.addressBits
    val srcFull  = Cat(srcHiReg, srcLoReg) + offReg
    val dstFull  = Cat(dstHiReg, dstLoReg) + offReg
    val srcAddr  = srcFull(addrBits - 1, 0)
    val dstAddr  = dstFull(addrBits - 1, 0)
    val addrTooWide =
      if (addrBits >= 64) false.B
      else (srcFull >> addrBits).orR || (dstFull >> addrBits).orR

    val getA = edge.Get(fromSource = 0.U, toAddress = srcAddr, lgSize = lgXfer.U)._2
    val putA = edge.Put(fromSource = 0.U, toAddress = dstAddr, lgSize = lgXfer.U, data = dataReg)._2

    mem.a.valid := (state === s_rd_req) || (state === s_wr_req)
    mem.a.bits  := Mux(state === s_wr_req, putA, getA)
    mem.d.ready := (state === s_rd_resp) || (state === s_wr_resp)
    mem.b.ready := false.B
    mem.c.valid := false.B
    mem.e.valid := false.B

    // A request the checker refuses comes back denied; surface it and stop so
    // the failure is unambiguous rather than being overwritten by later beats.
    val respBad = mem.d.bits.denied || mem.d.bits.corrupt

    when (state === s_rd_req && mem.a.fire) { state := s_rd_resp }

    when (state === s_rd_resp && mem.d.fire) {
      dataReg    := mem.d.bits.data
      errReg     := errReg || mem.d.bits.denied
      corruptReg := corruptReg || mem.d.bits.corrupt
      state      := Mux(respBad, s_idle, s_wr_req)
      when (respBad) { doneReg := true.B }
    }

    when (state === s_wr_req && mem.a.fire) { state := s_wr_resp }

    when (state === s_wr_resp && mem.d.fire) {
      errReg     := errReg || mem.d.bits.denied
      corruptReg := corruptReg || mem.d.bits.corrupt
      val nextOff = offReg + xferBytes.U
      offReg := nextOff
      when (respBad || nextOff >= lenReg) {
        state   := s_idle
        doneReg := true.B
      } .otherwise {
        state := s_rd_req
      }
    }

    val statusReg = Cat(0.U(27.W), addrErrReg, corruptReg, errReg, doneReg, busy)

    ctrlNode.regmap(
      WGDMAReg.srcLo -> Seq(RegField(32,
        RegReadFn({ valid => (true.B, srcLoReg) }),
        RegWriteFn({ (valid, data) => when (valid && !busy) { srcLoReg := data }; true.B }))),
      WGDMAReg.srcHi -> Seq(RegField(32,
        RegReadFn({ valid => (true.B, srcHiReg) }),
        RegWriteFn({ (valid, data) => when (valid && !busy) { srcHiReg := data }; true.B }))),
      WGDMAReg.dstLo -> Seq(RegField(32,
        RegReadFn({ valid => (true.B, dstLoReg) }),
        RegWriteFn({ (valid, data) => when (valid && !busy) { dstLoReg := data }; true.B }))),
      WGDMAReg.dstHi -> Seq(RegField(32,
        RegReadFn({ valid => (true.B, dstHiReg) }),
        RegWriteFn({ (valid, data) => when (valid && !busy) { dstHiReg := data }; true.B }))),
      WGDMAReg.len -> Seq(RegField(32,
        RegReadFn({ valid => (true.B, lenReg) }),
        RegWriteFn({ (valid, data) => when (valid && !busy) { lenReg := data }; true.B }))),
      WGDMAReg.ctrl -> Seq(RegField(32,
        RegReadFn({ valid => (true.B, Cat(0.U(31.W), busy)) }),
        RegWriteFn({ (valid, data) =>
          when (valid && data(0) && !busy && lenReg =/= 0.U) {
            offReg     := 0.U
            errReg     := false.B
            corruptReg := false.B
            addrErrReg := addrTooWide
            // An address that does not fit the bus is refused outright, with
            // DONE set so software's poll loop terminates.
            doneReg    := addrTooWide
            state      := Mux(addrTooWide, s_idle, s_rd_req)
          }
          true.B
        }))),
      WGDMAReg.status -> Seq(RegField.r(32, statusReg)),
      WGDMAReg.bytes  -> Seq(RegField.r(32, offReg))
    )
  }
}

case class WGDMAAttachParams(
  device: WGDMAParams,
  controlWhere: TLBusWrapperLocation = PBUS,
  // MBUS, not FBUS: on the front bus the engine's traffic goes through the L2,
  // so the memory-port checker judges L2 refills and write-backs rather than
  // the device's own transactions -- a Put that hits a resident line is acked
  // by the L2 and never checked at all, and a dirty line is written back later
  // under the directory's owner WID. Mastering the memory bus directly puts
  // every device transaction in front of the checker. The engine is then
  // non-coherent with the core's caches; software flushes around transfers.
  masterWhere: TLBusWrapperLocation = MBUS)
{
  def attachTo(where: Attachable)(implicit p: Parameters): WGDMA = where {
    val manager = where.locateTLBusWrapper(controlWhere)
    val client  = where.locateTLBusWrapper(masterWhere)

    // The engine needs a clock domain of its own; without one its LazyModuleImp
    // has no implicit clock. Follows the BlockDevice attach pattern.
    val domain = manager.generateSynchronousDomain.suggestName("wgdma_domain")
    val dma = domain { LazyModule(new WGDMA(device, manager.beatBytes)) }

    // The marker is a TLAdapterNode; placing it between the engine's master
    // port and the bus stamps `wid` on everything the engine issues.
    val wgm = WGMarkerAttachParams(WGMarkerParams(
      postfix  = "wgdma",
      wid      = device.wid,
      widWidth = device.widWidth,
      mwid     = device.mwid,
      address  = device.markerAddress,
      size     = device.markerSize)).attachTo(where)

    manager.coupleTo("wgdma_ctrl") {
      dma.ctrlNode := TLFragmenter(manager.beatBytes, manager.blockBytes) := _
    }
    client.coupleFrom("wgdma_mem") { _ := wgm.wgm_node := dma.memNode }

    dma
  }
}

trait CanHaveWGDMA { this: BaseSubsystem =>
  p(WGDMAKey).foreach { params => WGDMAAttachParams(params).attachTo(this) }
}

/**
 * @param wid  the WID the engine starts out stamping (reprogrammable at runtime)
 * @param mwid the WID allowed to reprogram the marker -- the monitor's world.
 *             Defaults to nWorlds-1, the same convention WithWorldGuard uses
 *             for every checker's mwid.
 */
class WithWGDMA(
  wid: Int = 1,
  mwid: Option[Int] = None,
  ctrlAddress: BigInt = 0x2030000L,
  markerAddress: BigInt = 0x2040000L
) extends Config((site, here, up) => {
  case WGDMAKey => Some(WGDMAParams(
    ctrlAddress   = ctrlAddress,
    markerAddress = markerAddress,
    wid           = wid,
    mwid          = mwid.getOrElse(site(NWorlds) - 1),
    widWidth      = log2Ceil(site(NWorlds))))
})
