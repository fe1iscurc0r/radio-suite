// SPDX-License-Identifier: AGPL-3.0-or-later
// RS-BA1 音频通路移植测试：TX 打包字节级对照 kappanhang + RX 重排语义
#include <catch2/catch_test_macros.hpp>

#include "core/audio_stream.h"

using namespace radio::core;
using rsba1::AudioRxBuffer;
using rsba1::AudioTxPacker;
using rsba1::kAudioFrameBytes;
using rsba1::kAudioPart1Pcm;
using rsba1::kAudioPart2Pcm;

namespace {
std::vector<uint8_t> frameWith(uint8_t fill) {
  return std::vector<uint8_t>(kAudioFrameBytes, fill);
}
}  // namespace

TEST_CASE("audio: TX packer header byte-exact vs kappanhang", "[audio]") {
  AudioTxPacker tx;
  tx.setSids(0x2A94BC02, 0x19F8B4F7);
  auto pkts = tx.packFrame(frameWith(0x11));
  REQUIRE(pkts.size() == 2);

  // part1：0x6c 0x05 + SID BE + 0x80 + seq-1 + len=1364 BE
  const auto& p1 = pkts[0];
  REQUIRE(p1.size() == 24 + kAudioPart1Pcm);
  CHECK(p1[0] == 0x6c);
  CHECK(p1[1] == 0x05);
  CHECK(p1[8] == 0x2A);   // localSID BE
  CHECK(p1[12] == 0x19);  // remoteSID BE
  CHECK(p1[16] == 0x80);
  CHECK(p1[18] == 0x00);  // (seq-1)=0 首帧
  CHECK(p1[19] == 0x00);
  CHECK(p1[22] == (kAudioPart1Pcm >> 8));
  CHECK(p1[23] == (kAudioPart1Pcm & 0xFF));
  CHECK(p1[24] == 0x11);  // PCM 载荷开头

  // part2：0x44 0x02 + len=556 + seq 已推到 2（包内 (seq-1)=1）
  const auto& p2 = pkts[1];
  REQUIRE(p2.size() == 24 + kAudioPart2Pcm);
  CHECK(p2[0] == 0x44);
  CHECK(p2[1] == 0x02);
  CHECK(p2[18] == 0x00);
  CHECK(p2[19] == 0x01);
  CHECK(p2[22] == (kAudioPart2Pcm >> 8));
  CHECK(p2[23] == (kAudioPart2Pcm & 0xFF));

  // 第二帧 seq 推进到 3/4
  auto pkts2 = tx.packFrame(frameWith(0x22));
  REQUIRE(pkts2.size() == 2);
  CHECK(pkts2[0][19] == 0x02);
  CHECK(pkts2[1][19] == 0x03);
}

TEST_CASE("audio: TX all-zero frame skipped", "[audio]") {
  AudioTxPacker tx;
  tx.setSids(1, 2);
  CHECK(tx.packFrame(std::vector<uint8_t>(kAudioFrameBytes, 0)).empty());
  // 长度不符也拒绝
  CHECK(tx.packFrame(std::vector<uint8_t>(100, 1)).empty());
}

TEST_CASE("audio: RX in-order delivery and seq advance", "[audio]") {
  AudioRxBuffer rx;
  auto make = [](uint16_t seq, size_t payload_len) {
    std::vector<uint8_t> p(24 + payload_len, 0);
    p[0] = 0x6c; p[1] = 0x05;
    p[6] = static_cast<uint8_t>(seq & 0xFF);
    p[7] = static_cast<uint8_t>(seq >> 8);
    for (size_t i = 24; i < p.size(); ++i) p[i] = 0xAA;
    return p;
  };

  auto e1 = rx.feed(make(100, 1364), 0);
  REQUIRE(e1.ready.size() == 1);
  CHECK(e1.ready[0].seq == 100);
  CHECK(e1.ready[0].pcm.size() == 1364);
  CHECK_FALSE(e1.dropped);

  auto e2 = rx.feed(make(101, 556), 1);
  REQUIRE(e2.ready.size() == 1);
  CHECK(e2.ready[0].seq == 101);
  CHECK(e2.ready[0].pcm.size() == 556);
  CHECK(rx.lostPackets() == 0);
  CHECK(rx.lastDeliveredSeq() == 101);
  CHECK(rx.pendingPackets() == 0);
}

TEST_CASE("audio: RX reorder window buffers early packets", "[audio]") {
  AudioRxBuffer rx;
  auto make = [](uint16_t seq) {
    std::vector<uint8_t> p(24 + kAudioPart2Pcm, 0);  // part2 包头（580B 下限）
    p[0] = 0x44; p[1] = 0x02;
    p[6] = static_cast<uint8_t>(seq & 0xFF);
    p[7] = static_cast<uint8_t>(seq >> 8);
    return p;
  };

  CHECK(rx.feed(make(100), 0).ready.size() == 1);
  // 102 早到：进重排窗，不投递
  auto early = rx.feed(make(102), 10);
  CHECK(early.ready.empty());
  CHECK_FALSE(early.dropped);
  CHECK(rx.pendingPackets() == 1);
  // 窗口未到期不强行放行
  CHECK(rx.flush(50).empty());
  // 101 补上：101、102 一起按序投递
  auto fill = rx.feed(make(101), 60);
  REQUIRE(fill.ready.size() == 2);
  CHECK(fill.ready[0].seq == 101);
  CHECK(fill.ready[1].seq == 102);
  CHECK(rx.lostPackets() == 0);
  CHECK(rx.pendingPackets() == 0);
}

TEST_CASE("audio: RX gap releases after 100ms window", "[audio]") {
  AudioRxBuffer rx;
  auto make = [](uint16_t seq) {
    std::vector<uint8_t> p(24 + kAudioPart2Pcm, 0);
    p[0] = 0x6c; p[1] = 0x05;
    p[6] = static_cast<uint8_t>(seq & 0xFF);
    p[7] = static_cast<uint8_t>(seq >> 8);
    return p;
  };

  CHECK(rx.feed(make(200), 0).ready.size() == 1);
  auto buffered = rx.feed(make(203), 5);  // 201/202 缺失
  CHECK(buffered.ready.empty());
  CHECK(rx.flush(100).empty());  // 95ms < 100ms
  auto released = rx.flush(106);  // 101ms ≥ 100ms
  REQUIRE(released.size() == 1);
  CHECK(released[0].seq == 203);
  CHECK(rx.lostPackets() == 2);
  CHECK(rx.lastDeliveredSeq() == 203);
  CHECK(rx.pendingPackets() == 0);

  // 迟到（被窗口判丢后）重传包：丢弃计数
  auto late = rx.feed(make(201), 120);
  CHECK(late.dropped);
  CHECK(late.ready.empty());
  CHECK(rx.droppedPackets() == 1);
}

TEST_CASE("audio: RX duplicate dropped, seq wraps", "[audio]") {
  AudioRxBuffer rx;
  auto make = [](uint16_t seq) {
    std::vector<uint8_t> p(24 + kAudioPart2Pcm, 0);
    p[0] = 0x44; p[1] = 0x02;
    p[6] = static_cast<uint8_t>(seq & 0xFF);
    p[7] = static_cast<uint8_t>(seq >> 8);
    return p;
  };

  CHECK(rx.feed(make(0xFFFF), 0).ready.size() == 1);
  CHECK(rx.feed(make(0x0000), 1).ready.size() == 1);  // 环绕：FFFF→0 视为连续
  CHECK(rx.feed(make(0x0001), 2).ready.size() == 1);
  CHECK(rx.lostPackets() == 0);

  auto dup = rx.feed(make(0x0001), 3);
  CHECK(dup.dropped);
  CHECK(rx.droppedPackets() == 1);
}

TEST_CASE("audio: non-audio packets ignored", "[audio]") {
  AudioRxBuffer rx;
  std::vector<uint8_t> junk(600, 0x00);  // 头不匹配
  CHECK(rx.feed(junk, 0).ready.empty());
  CHECK_FALSE(rx.receivedAnything());
  // 包头对但总长不足 580（上游 handleRead 门槛）：忽略
  std::vector<uint8_t> short_pkt(24 + 100, 0);
  short_pkt[0] = 0x6c; short_pkt[1] = 0x05;
  CHECK(rx.feed(short_pkt, 0).ready.empty());
  CHECK_FALSE(rx.receivedAnything());
}
