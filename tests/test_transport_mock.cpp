// SPDX-License-Identifier: AGPL-3.0-or-later
// MockTransport 闭环测试（TEST_CASE 名保持 ASCII，理由见 test_civ.cpp 头注）
#include <catch2/catch_test_macros.hpp>

#include "core/civ.h"
#include "core/transport_mock.h"

using namespace radio::core;

TEST_CASE("mock: read-freq loopback default 14.270 MHz", "[mock]") {
  MockTransport mt;
  REQUIRE(mt.open(nullptr));
  std::optional<civ::CivFrame> got;
  mt.setFrameCallback([&](std::span<const uint8_t> raw) {
    got = civ::parseFrame(raw);
  });

  CHECK(mt.send(civ::readFreqFrame()));
  CHECK(mt.poll() == 1);
  REQUIRE(got.has_value());
  auto hz = civ::parseFreqResponse(*got);
  REQUIRE(hz.has_value());
  CHECK(*hz == 14'270'000ULL);
}

TEST_CASE("mock: set-freq in-band ACK / out-of-band NAK", "[mock]") {
  MockTransport mt;
  mt.open(nullptr);
  std::vector<civ::CivFrame> frames;
  mt.setFrameCallback([&](std::span<const uint8_t> raw) {
    if (auto f = civ::parseFrame(raw)) frames.push_back(*f);
  });

  SECTION("带内 7.100 MHz → ACK 且生效") {
    CHECK(mt.send(civ::setFreqFrame(7'100'000)));
    mt.poll();
    REQUIRE(frames.size() == 1);
    CHECK(civ::isAck(frames[0]));
    CHECK(mt.sim().freq_hz == 7'100'000);
  }
  SECTION("广播段 98 MHz 可收听（RX 开放）→ ACK 生效") {
    CHECK(mt.send(civ::setFreqFrame(98'000'000)));
    mt.poll();
    REQUIRE(frames.size() == 1);
    CHECK(civ::isAck(frames[0]));
    CHECK(mt.sim().freq_hz == 98'000'000);
  }
  SECTION("接收范围外 2.4 GHz → NAK 且不变") {
    CHECK(mt.send(civ::setFreqFrame(2'400'000'000)));
    mt.poll();
    REQUIRE(frames.size() == 1);
    CHECK(civ::isNak(frames[0]));
    CHECK(mt.sim().freq_hz == 14'270'000);
  }
}

TEST_CASE("mock: ATT only settable on HF/50MHz", "[mock]") {
  MockTransport mt;
  mt.open(nullptr);
  std::vector<civ::CivFrame> frames;
  mt.setFrameCallback([&](std::span<const uint8_t> raw) {
    if (auto f = civ::parseFrame(raw)) frames.push_back(*f);
  });

  SECTION("HF 段：ATT on → ACK，回读 0x20") {
    CHECK(mt.send(civ::setFreqFrame(7'100'000)));
    mt.poll();
    frames.clear();
    CHECK(mt.send(civ::attFrame(true)));
    mt.poll();
    CHECK(civ::isAck(frames[0]));
    frames.clear();
    CHECK(mt.send(civ::readAttFrame()));
    mt.poll();
    auto att = civ::parseAttResponse(frames[0]);
    REQUIRE(att.has_value());
    CHECK(*att == 0x20);
  }
  SECTION("VHF 段：ATT → NAK（真机拒绝）") {
    CHECK(mt.send(civ::setFreqFrame(145'500'000)));
    mt.poll();
    frames.clear();
    CHECK(mt.send(civ::attFrame(true)));
    mt.poll();
    CHECK(civ::isNak(frames[0]));
    CHECK_FALSE(mt.sim().att_20db);
  }
}

TEST_CASE("mock: duplex write/read with simplex normalization", "[mock]") {
  MockTransport mt;
  mt.open(nullptr);
  std::vector<civ::CivFrame> frames;
  mt.setFrameCallback([&](std::span<const uint8_t> raw) {
    if (auto f = civ::parseFrame(raw)) frames.push_back(*f);
  });

  CHECK(mt.send(civ::duplexFrame(0x11)));  // DUP-
  mt.poll();
  REQUIRE(frames.size() == 1);
  CHECK(civ::isAck(frames[0]));

  frames.clear();
  CHECK(mt.send(civ::readDuplexFrame()));
  mt.poll();
  auto d = civ::parseDuplexResponse(frames[0]);
  REQUIRE(d.has_value());
  CHECK(*d == 0x11);

  // 真机语义：simplex 写 0x10，读回归一化为 0x00（radio_link 2026-08-24 实测）
  CHECK(mt.send(civ::duplexFrame(0x10)));
  mt.poll();
  frames.clear();
  CHECK(mt.send(civ::readDuplexFrame()));
  mt.poll();
  d = civ::parseDuplexResponse(frames[0]);
  REQUIRE(d.has_value());
  CHECK(*d == 0x00);

  // 非法值 → NAK
  frames.clear();
  CHECK(mt.send(civ::duplexFrame(0x07)));
  mt.poll();
  CHECK(civ::isNak(frames[0]));
}

TEST_CASE("mock: sw16 NB set and readback", "[mock]") {
  MockTransport mt;
  mt.open(nullptr);
  std::vector<civ::CivFrame> frames;
  mt.setFrameCallback([&](std::span<const uint8_t> raw) {
    if (auto f = civ::parseFrame(raw)) frames.push_back(*f);
  });

  CHECK(mt.send(civ::sw16SetFrame(civ::kSub16Nb, 0x01)));
  mt.poll();
  CHECK(civ::isAck(frames[0]));

  frames.clear();
  CHECK(mt.send(civ::sw16ReadFrame(civ::kSub16Nb)));
  mt.poll();
  auto nb = civ::parseSw16Response(frames[0], civ::kSub16Nb);
  REQUIRE(nb.has_value());
  CHECK(*nb == 1);
  CHECK(mt.sim().nb);

  // 非法值 → NAK
  frames.clear();
  CHECK(mt.send(civ::sw16SetFrame(civ::kSub16Nb, 0x07)));
  mt.poll();
  CHECK(civ::isNak(frames[0]));
  CHECK(mt.sim().nb);
}

TEST_CASE("mock: PTT on ACK and readback", "[mock]") {
  MockTransport mt;
  mt.open(nullptr);
  std::vector<civ::CivFrame> frames;
  mt.setFrameCallback([&](std::span<const uint8_t> raw) {
    if (auto f = civ::parseFrame(raw)) frames.push_back(*f);
  });

  CHECK(mt.send(civ::pttFrame(true)));
  mt.poll();
  REQUIRE(frames.size() == 1);
  CHECK(civ::isAck(frames[0]));
  CHECK(mt.sim().ptt);

  frames.clear();
  CHECK(mt.send(civ::readPttFrame()));
  mt.poll();
  REQUIRE(frames.size() == 1);
  auto ptt = civ::parsePttResponse(frames[0]);
  REQUIRE(ptt.has_value());
  CHECK(*ptt == true);
}

TEST_CASE("mock: mode and smeter responses", "[mock]") {
  MockTransport mt;
  mt.open(nullptr);
  MockTransport::SimConfig cfg = mt.sim();
  cfg.mode = civ::Mode::FM;
  cfg.filter = 2;
  cfg.smeter_raw = 77;
  mt.setSim(cfg);

  std::vector<civ::CivFrame> frames;
  mt.setFrameCallback([&](std::span<const uint8_t> raw) {
    if (auto f = civ::parseFrame(raw)) frames.push_back(*f);
  });

  mt.send(civ::readModeFrame());
  mt.send(civ::readSmeterFrame());
  CHECK(mt.poll() == 2);
  REQUIRE(frames.size() == 2);
  auto mi = civ::parseModeResponse(frames[0]);
  REQUIRE(mi.has_value());
  CHECK(mi->mode == civ::Mode::FM);
  CHECK(mi->filter == 2);
  auto sm = civ::parseSmeterResponse(frames[1]);
  REQUIRE(sm.has_value());
  CHECK(*sm == 77);
}

TEST_CASE("mock: send fails when closed; garbage yields no response", "[mock]") {
  MockTransport mt;  // 未 open
  CHECK_FALSE(mt.send(civ::readFreqFrame()));

  mt.open(nullptr);
  std::vector<civ::CivFrame> frames;
  mt.setFrameCallback([&](std::span<const uint8_t> raw) {
    if (auto f = civ::parseFrame(raw)) frames.push_back(*f);
  });
  const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
  CHECK_FALSE(mt.send(garbage));
  CHECK(mt.poll() == 0);
  CHECK(frames.empty());
}
