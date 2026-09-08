/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// See LICENSE for license details.

/**
 * Author: Sungkeun Kim (sk84.kim@samsung.com)
 */

package worldguard

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config._
import freechips.rocketchip.diplomacy._


import freechips.rocketchip.regmapper._
import freechips.rocketchip.tilelink._

import sifive.blocks.inclusivecache._

class WGInclusiveCacheControl(outer: WGInclusiveCache, control: InclusiveCacheControlParameters)(implicit p: Parameters) extends LazyModule()(p) {
  val ctrlnode = TLRegisterNode(
    address     = Seq(AddressSet(control.address, InclusiveCacheParameters.L2ControlSize-1)),
    device      = outer.device,
    concurrency = 1, // Only one flush at a time (else need to track who answers)
    beatBytes   = control.beatBytes)

  lazy val module = new Impl
  class Impl extends LazyModuleImp(this) {
    val io = IO(new Bundle {
      val flush_match = Input(Bool())
      val flush_req = Decoupled(UInt(64.W))
      val flush_resp = Input(Bool())
      val dbg = Input(Vec(16, UInt(32.W)))  // WG debug counters from the scheduler
      // WID applied to subsequent flush requests. SW writes it before issuing a flush so
      // the flush hits the owning WID's lines (clean same-WID hit) instead of taking the
      // heavy cross-WID onlyTagHit path. Default 0 preserves the legacy behavior.
      val flush_wid = Output(UInt(8.W))
    })
    // WID for flush requests (see io.flush_wid). RW via the FlushWid control register.
    val flushWid = RegInit(0.U(8.W))
    io.flush_wid := flushWid
    // Flush directive
    val flushInValid   = RegInit(false.B)
    val flushInAddress = Reg(UInt(64.W))
    val flushOutValid  = RegInit(false.B)
    val flushOutReady  = WireInit(init = false.B)

    when (flushOutReady) { flushOutValid := false.B }
    when (io.flush_resp) { flushOutValid := true.B }
    when (io.flush_req.ready) { flushInValid := false.B }
    io.flush_req.valid := flushInValid
    io.flush_req.bits := flushInAddress

    when (!io.flush_match && flushInValid) {
      flushInValid := false.B
      flushOutValid := true.B
    }

    val flush32 = RegField.w(32, RegWriteFn((ivalid, oready, data) => {
      when (oready) { flushOutReady := true.B }
      when (ivalid) { flushInValid := true.B }
      when (ivalid && !flushInValid) { flushInAddress := data << 4 }
      (!flushInValid, flushOutValid)
    }), RegFieldDesc("Flush32", "Flush the physical address equal to the 32-bit written data << 4 from the cache"))

    val flush64 = RegField.w(64, RegWriteFn((ivalid, oready, data) => {
      when (oready) { flushOutReady := true.B }
      when (ivalid) { flushInValid := true.B }
      when (ivalid && !flushInValid) { flushInAddress := data }
      (!flushInValid, flushOutValid)
    }), RegFieldDesc("Flush64", "Flush the phsyical address equal to the 64-bit written data from the cache"))


    // Information about the cache configuration
    val banksR  = RegField.r(8, outer.node.edges.in.size.U,         RegFieldDesc("Banks",
      "Number of banks in the cache", reset=Some(outer.node.edges.in.size)))
    val waysR   = RegField.r(8, outer.cache.ways.U,                 RegFieldDesc("Ways",
      "Number of ways per bank", reset=Some(outer.cache.ways)))
    val lgSetsR = RegField.r(8, log2Ceil(outer.cache.sets).U,       RegFieldDesc("lgSets",
      "Base-2 logarithm of the sets per bank", reset=Some(log2Ceil(outer.cache.sets))))
    val lgBlockBytesR = RegField.r(8, log2Ceil(outer.cache.blockBytes).U, RegFieldDesc("lgBlockBytes",
      "Base-2 logarithm of the bytes per cache block", reset=Some(log2Ceil(outer.cache.blockBytes))))

    val flushWidField = RegField(8, flushWid, RegFieldDesc("FlushWid",
      "WID applied to subsequent flush requests (0 = legacy cross-WID behavior)"))

    // WG debug counters (read-only): 0x300 cross-WID onlyTagHit, 0x308 releases, 0x310 stall-cycles
    val dbgOth   = RegField.r(32, io.dbg(0), RegFieldDesc("dbgOth",   "cross-WID onlyTagHit(!hit) count", volatile=true))
    val dbgRel   = RegField.r(32, io.dbg(1), RegFieldDesc("dbgRel",   "outer release fires", volatile=true))
    val dbgStall = RegField.r(32, io.dbg(2), RegFieldDesc("dbgStall", "request not-accepted cycles", volatile=true))
    // fetch-path 진단: 0x318 outer-A(Get to mem) fires, 0x320 outer-D(grant from mem) fires,
    // 0x328 accepted requests tagged wid==1 (enclave fetch reaching L2).
    val dbgOutA  = RegField.r(32, io.dbg(3), RegFieldDesc("dbgOutA",  "outer A (Get/Put to mem) fires", volatile=true))
    val dbgOutD  = RegField.r(32, io.dbg(4), RegFieldDesc("dbgOutD",  "outer D (grant/ack from mem) fires", volatile=true))
    val dbgReqW1 = RegField.r(32, io.dbg(5), RegFieldDesc("dbgReqW1", "accepted requests tagged wid==1", volatile=true))
    // inner 채널 총량: 0x330 코어로 나간 grant, 0x338 코어에서 들어온 요청.
    val dbgInD   = RegField.r(32, io.dbg(6), RegFieldDesc("dbgInD",   "inner D fires (grants to core)", volatile=true))
    val dbgInA   = RegField.r(32, io.dbg(7), RegFieldDesc("dbgInA",   "inner A fires (requests from core)", volatile=true))
    // 0x340 + 8*w: EPM 주소창(0x8300_0000..0x8800_0000) 요청 수를 WID w별로 집계.
    val dbgEpmWid = Seq.tabulate(8) { w =>
      RegField.r(32, io.dbg(8 + w), RegFieldDesc(s"dbgEpmWid$w", s"EPM-window accepted requests tagged wid==$w", volatile=true))
    }

    val regmapEntries: Seq[(Int, Seq[RegField])] = Seq(
      0x000 -> RegFieldGroup("Config", Some("Information about the Cache Configuration"), Seq(banksR, waysR, lgSetsR, lgBlockBytesR)),
      0x200 -> (if (control.beatBytes >= 8) Seq(flush64) else Nil),
      0x218 -> Seq(flushWidField),
      0x240 -> Seq(flush32),
      0x300 -> Seq(dbgOth),
      0x308 -> Seq(dbgRel),
      0x310 -> Seq(dbgStall),
      0x318 -> Seq(dbgOutA),
      0x320 -> Seq(dbgOutD),
      0x328 -> Seq(dbgReqW1),
      0x330 -> Seq(dbgInD),
      0x338 -> Seq(dbgInA)
    ) ++ (0 until 8).map { w => (0x340 + 8 * w) -> Seq(dbgEpmWid(w)) }

    val regmap = ctrlnode.regmap(regmapEntries: _*)
  }
}
