// SPDX-License-Identifier: AGPL-3.0-or-later
// RS-BA1 本机环回：假电台服务端（127.0.0.1 三信道）+ 真 Rsba1UdpTransport。
// 只验协议与编排，不需要 IC-705；真机通路仍由 docs/HANDOVER.md 的真机验证段落补。
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef RADIO_SUITE_HAVE_UDP
#include "core/transport_udp.h"
#include "fake_rsba1_server.h"
#include "hex_util.h"

using namespace radio::core;

namespace {

constexpr size_t kPart1 = rsba1::kAudioPart1Pcm;   // 1364
constexpr size_t kPart2 = rsba1::kAudioPart2Pcm;   // 556

std::string fixturePath(const std::string& name) {
  return std::string(RADIO_SUITE_FIXTURES_DIR) + "/" + name;
}

template <typename F>
bool waitUntil(F&& pred, int timeout_ms = 2000) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return pred();
}

// 环回会话：假电台 + 客户端；析构时客户端 close()，fake 随之收到拆除包
struct Loopback {
  FakeRsba1Server srv;
  std::unique_ptr<Rsba1UdpTransport> transport;
  std::string err;

  bool up() {
    if (!srv.start(&err)) return false;
    Rsba1UdpTransport::Config cfg;
    cfg.host = "127.0.0.1";
    cfg.username = "linnan";
    cfg.password = "test123";
    cfg.cmd_port = srv.ctrlPort();
    cfg.ser_port = srv.serialPort();
    cfg.aud_port = srv.audioPort();
    cfg.handshake_timeout_ms = 2000;
    transport = std::make_unique<Rsba1UdpTransport>(std::move(cfg));
    return transport->open(&err);
  }

  bool pumpUntil(const std::function<bool()>& pred, int timeout_ms = 2000) {
    return waitUntil(
        [&] {
          transport->poll();
          return pred();
        },
        timeout_ms);
  }
};

}  // namespace

TEST_CASE("loopback: fake radio replies match IC-705 captures", "[rsba1][loopback]") {
  const std::array<uint8_t, 6> auth = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  // fixtures 由 rsba1-core 参考实现生成（IC-705 真机验证 2026-08-18）：
  // 假服务端的应答必须与真机报文逐字节一致，环回测试才有意义。
  CHECK(radio::test::toHex(FakeRsba1Server::buildLoginReply(auth)) ==
        radio::test::toHex(
            radio::test::loadSingleFrame(fixturePath("udp_login_resp_ok.hex"))));
  CHECK(radio::test::toHex(FakeRsba1Server::buildConnectTransReply(
            0x11111111, 0x22222222, auth, "IC-705")) ==
        radio::test::toHex(radio::test::loadSingleFrame(
            fixturePath("udp_connecttrans_resp.hex"))));
}

TEST_CASE("loopback: handshake brings up CI-V and 50003 audio", "[rsba1][loopback]") {
  Loopback lb;
  INFO(lb.err);
  REQUIRE(lb.up());
  CHECK(lb.transport->isOpen());
  CHECK(lb.transport->audioReady());
  CHECK(lb.transport->lastLog().find("dev=IC-705") != std::string::npos);
  CHECK(lb.transport->lastLog().find("audio: 双通路就绪") != std::string::npos);
  CHECK(lb.srv.serialOpened());
}

TEST_CASE("loopback: CI-V uplink and downlink", "[rsba1][loopback]") {
  Loopback lb;
  INFO(lb.err);
  REQUIRE(lb.up());

  const std::vector<uint8_t> cmd = {0xFE, 0xFE, 0xE0, 0x03, 0xFD};  // read freq
  REQUIRE(lb.transport->send(cmd));
  REQUIRE(waitUntil([&] { return !lb.srv.clientCivFrames().empty(); }));
  CHECK(lb.srv.clientCivFrames()[0] == cmd);

  std::vector<uint8_t> delivered;
  lb.transport->setFrameCallback([&](std::span<const uint8_t> f) {
    delivered.assign(f.begin(), f.end());
  });
  const std::vector<uint8_t> resp = {0xFE, 0xFE, 0xE0, 0x03,
                                     0x00, 0x50, 0x14, 0x27, 0x00, 0xFD};
  lb.srv.sendCiv(resp);
  REQUIRE(lb.pumpUntil([&] { return !delivered.empty(); }));
  CHECK(delivered == resp);
}

TEST_CASE("loopback: TX audio wire format and reassembly", "[rsba1][loopback]") {
  Loopback lb;
  INFO(lb.err);
  REQUIRE(lb.up());

  std::vector<int16_t> pcm(rsba1::kAudioFrameBytes / 2);
  for (size_t i = 0; i < pcm.size(); ++i)
    pcm[i] = static_cast<int16_t>(static_cast<int>(i * 3) - 2000);

  REQUIRE(lb.transport->sendTxAudio(pcm));
  REQUIRE(waitUntil([&] { return !lb.srv.clientAudioFrames().empty(); }));
  const auto frames = lb.srv.clientAudioFrames();
  REQUIRE(frames.size() == 1);
  REQUIRE(frames[0].size() == rsba1::kAudioFrameBytes);
  CHECK(std::memcmp(frames[0].data(), pcm.data(), rsba1::kAudioFrameBytes) == 0);

  const auto pkts = lb.srv.clientAudioPackets();
  REQUIRE(pkts.size() == 2);
  // part1：0x6c05，24B 头 + 1364B；part2：0x4402，24B 头 + 556B
  CHECK(pkts[0][0] == 0x6c);
  CHECK(pkts[0][1] == 0x05);
  CHECK(pkts[0].size() == 24 + kPart1);
  CHECK(pkts[0][16] == 0x80);
  CHECK(pkts[0][22] == 0x05);  // 1364 = 0x0554
  CHECK(pkts[0][23] == 0x54);
  CHECK(pkts[1][0] == 0x44);
  CHECK(pkts[1][1] == 0x02);
  CHECK(pkts[1].size() == 24 + kPart2);
  // 头里的 remoteSID = 音频信道握手时假电台下发的 SID
  CHECK(pkts[0][12] == 0x0C);
  CHECK(pkts[0][13] == 0x0D);
  CHECK(pkts[0][14] == 0x0E);
  CHECK(pkts[0][15] == 0x0F);

  // 第二帧：(seq-1) 续推 2/3
  REQUIRE(lb.transport->sendTxAudio(pcm));
  REQUIRE(waitUntil([&] { return lb.srv.clientAudioFrames().size() == 2; }));
  const auto pkts2 = lb.srv.clientAudioPackets();
  REQUIRE(pkts2.size() == 4);
  CHECK(pkts2[2][19] == 0x02);
  CHECK(pkts2[3][19] == 0x03);

  // 全零帧：上游 isAllZero 跳发（返回 true 但线上无包）
  REQUIRE(lb.transport->sendTxAudio(
      std::vector<int16_t>(rsba1::kAudioFrameBytes / 2, 0)));
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  CHECK(lb.srv.clientAudioPackets().size() == 4);
}

TEST_CASE("loopback: RX audio order, reorder window and loss release",
          "[rsba1][loopback]") {
  Loopback lb;
  INFO(lb.err);
  REQUIRE(lb.up());

  std::vector<int> chunk_head;  // 每块首个 int16，用来判顺序与内容
  std::vector<size_t> chunk_bytes;
  lb.transport->setRxAudioCallback([&](std::span<const int16_t> pcm) {
    if (!pcm.empty()) chunk_head.push_back(pcm.front());
    chunk_bytes.push_back(pcm.size() * sizeof(int16_t));
  });

  auto sendChunk = [&](uint16_t seq, uint8_t fill, size_t len) {
    std::vector<uint8_t> pcm(len, fill);
    lb.srv.sendAudio(pcm, seq);
  };

  // 顺序流：10(part1) → 11(part2) 立即投递
  sendChunk(10, 0x10, kPart1);
  sendChunk(11, 0x11, kPart2);
  REQUIRE(lb.pumpUntil([&] { return chunk_head.size() == 2; }));
  CHECK(chunk_head[0] == 0x1010);
  CHECK(chunk_head[1] == 0x1111);
  CHECK(chunk_bytes[0] == kPart1);
  CHECK(chunk_bytes[1] == kPart2);

  // 乱序：13、14 先到（缓存），隔 20ms 补 12 → 12、13、14 按序投递
  chunk_head.clear();
  sendChunk(13, 0x13, kPart2);
  sendChunk(14, 0x14, kPart2);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK(chunk_head.empty());  // 缺口未补，不投递
  sendChunk(12, 0x12, kPart2);
  REQUIRE(lb.pumpUntil([&] { return chunk_head.size() == 3; }));
  CHECK(chunk_head[0] == 0x1212);
  CHECK(chunk_head[1] == 0x1313);
  CHECK(chunk_head[2] == 0x1414);

  // 丢包：18 到达时 15..17 缺失，重排窗 100ms 后放行
  chunk_head.clear();
  sendChunk(18, 0x18, kPart2);
  REQUIRE(lb.pumpUntil([&] { return chunk_head.size() == 1; }, 1000));
  CHECK(chunk_head[0] == 0x1818);

  // 迟到重传包：不再投递
  chunk_head.clear();
  sendChunk(17, 0x17, kPart2);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  CHECK(chunk_head.empty());
}

TEST_CASE("loopback: close sends the teardown sequence", "[rsba1][loopback]") {
  Loopback lb;
  INFO(lb.err);
  REQUIRE(lb.up());
  REQUIRE(lb.srv.serialOpened());

  lb.transport->close();
  CHECK_FALSE(lb.transport->isOpen());
  // 五件套：serial close → audio disconnect → deauth → serial/ctrl disconnect
  CHECK(waitUntil([&] { return lb.srv.disconnectCount() >= 3; }, 1000));
  CHECK(lb.srv.gotDeauth());
  CHECK(lb.srv.serialClosed());
}

#endif  // RADIO_SUITE_HAVE_UDP
