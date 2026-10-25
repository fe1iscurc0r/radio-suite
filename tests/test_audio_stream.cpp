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
  // 构造合法 RX 包：0x6c 头 + seq LE + 载荷
  auto make = [](uint16_t seq, size_t payload_len) {
    std::vector<uint8_t> p(24 + payload_len, 0);
    p[0] = 0x6c; p[1] = 0x05;
    p[6] = static_cast<uint8_t>(seq & 0xFF);
    p[7] = static_cast<uint8_t>(seq >> 8);
    for (size_t i = 24; i < p.size(); ++i) p[i] = 0xAA;
    return p;
  };

  auto e1 = rx.feed(make(100, 100));
  CHECK(e1.delivered);
  CHECK(e1.pcm.size() == 100);

  auto e2 = rx.feed(make(101, 100));
  CHECK(e2.delivered);
  CHECK(e2.lost == 0);

  // 缺一包（102 丢）：103 到达报 lost=1
  auto e3 = rx.feed(make(103, 100));
  CHECK(e3.delivered);
  CHECK(e3.lost == 1);

  // 迟到的 102（重传）：丢弃
  auto e4 = rx.feed(make(102, 100));
  CHECK(e4.dropped);
  CHECK_FALSE(e4.delivered);
}

TEST_CASE("audio: non-audio packets ignored", "[audio]") {
  AudioRxBuffer rx;
  std::vector<uint8_t> junk(600, 0x00);  // 头不匹配
  CHECK_FALSE(rx.feed(junk).delivered);
  std::vector<uint8_t> short_pkt(10, 0x6c);
  CHECK_FALSE(rx.feed(short_pkt).delivered);
}
