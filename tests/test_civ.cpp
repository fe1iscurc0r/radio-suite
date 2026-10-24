// SPDX-License-Identifier: AGPL-3.0-or-later
// CI-V 编解码对照测试：字节级对齐 Python 参考实现与工单帧示例
// 注：TEST_CASE 名保持 ASCII —— Catch2 经 ctest 的发现机制在 Windows 上会
//     损坏非 ASCII 命令行参数（GBK/UTF-8），中文描述放注释与 SECTION。
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "core/civ.h"
#include "hex_util.h"

using namespace radio::core;
using Catch::Approx;
using radio::test::toHex;

namespace {
std::string fixture(const std::string& name) {
  return std::string(RADIO_SUITE_FIXTURES_DIR) + "/" + name;
}
}  // namespace

// 工单 M0：构造 FE FE A4 E0 03 FD
TEST_CASE("civ: read-freq frame matches workorder bytes", "[civ]") {
  auto f = civ::readFreqFrame(0xA4, 0xE0);
  REQUIRE(toHex(f) == "fefea4e003fd");
}

TEST_CASE("civ: frame build/parse roundtrip", "[civ]") {
  const uint8_t body[] = {0x1C, 0x00, 0x01};
  auto f = civ::buildFrame(0xA4, 0xE0, body);
  REQUIRE(f.size() == 8);  // FE FE A4 E0 1C 00 01 FD
  auto p = civ::parseFrame(f);
  REQUIRE(p.has_value());
  CHECK(p->to == 0xA4);
  CHECK(p->from == 0xE0);
  CHECK(p->cmd == 0x1C);
  REQUIRE(p->payload.size() == 2);
  CHECK(p->payload[0] == 0x00);
  CHECK(p->payload[1] == 0x01);

  SECTION("多余前导 FE 也接受（总线上 FE 用于帧同步填充）") {
    auto f3 = civ::buildFrame(0xA4, 0xE0, body, 4);
    auto p3 = civ::parseFrame(f3);
    REQUIRE(p3.has_value());
    CHECK(p3->cmd == 0x1C);
  }
}

TEST_CASE("civ: malformed frames rejected", "[civ]") {
  const uint8_t too_short[] = {0xFE, 0xFE, 0xA4, 0xE0, 0xFD};        // 缺 cmd：5 字节
  const uint8_t no_tail[] = {0xFE, 0xFE, 0xA4, 0xE0, 0x03};          // 无 FD 尾
  const uint8_t no_head[] = {0x00, 0xFE, 0xA4, 0xE0, 0x03, 0xFD};    // 无 FE 头
  const uint8_t all_pre[] = {0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE};    // 全前导
  CHECK_FALSE(civ::parseFrame(too_short));
  CHECK_FALSE(civ::parseFrame(no_tail));
  CHECK_FALSE(civ::parseFrame(no_head));
  CHECK_FALSE(civ::parseFrame(all_pre));
  std::vector<uint8_t> empty;
  CHECK_FALSE(civ::parseFrame(empty));
}

TEST_CASE("civ: freq BCD codec (5 bytes LSB-first)", "[civ]") {
  auto b14 = civ::freqToBcd5(14'270'000);
  REQUIRE(b14.has_value());
  CHECK(toHex(*b14) == "0000271400");

  auto b145 = civ::freqToBcd5(145'000'000);
  REQUIRE(b145.has_value());
  CHECK(toHex(*b145) == "0000004501");

  // 满量程与越界
  CHECK(civ::freqToBcd5(9'999'999'999ULL).has_value());
  CHECK_FALSE(civ::freqToBcd5(10'000'000'000ULL).has_value());
  auto b0 = civ::freqToBcd5(0);
  REQUIRE(b0.has_value());
  CHECK(toHex(*b0) == "0000000000");

  // 解码
  CHECK(*civ::bcdToFreq(std::span<const uint8_t>(*b14)) == 14'270'000);
  const uint8_t bad[] = {0x00, 0xAB, 0x00, 0x00, 0x00};  // 半字节 >9
  CHECK_FALSE(civ::bcdToFreq(bad));
  CHECK_FALSE(civ::bcdToFreq(std::span<const uint8_t>()));
  const uint8_t six[6] = {0};
  CHECK_FALSE(civ::bcdToFreq(six));
}

TEST_CASE("civ: fixtures generated from python reference", "[civ][fixtures]") {
  SECTION("读频率命令") {
    auto raw = radio::test::loadSingleFrame(fixture("read_freq_cmd.hex"));
    CHECK(toHex(raw) == "fefea4e003fd");
    auto p = civ::parseFrame(raw);
    REQUIRE(p.has_value());
    CHECK(p->cmd == civ::kCmdReadFreq);
  }
  SECTION("读频率应答 14.270 MHz") {
    auto raw = radio::test::loadSingleFrame(fixture("read_freq_resp_14270k.hex"));
    auto p = civ::parseFrame(raw);
    REQUIRE(p.has_value());
    auto hz = civ::parseFreqResponse(*p);
    REQUIRE(hz.has_value());
    CHECK(*hz == 14'270'000ULL);
  }
  SECTION("设频率命令 145 MHz（真机修正：cmd=0x05）") {
    auto raw = radio::test::loadSingleFrame(fixture("set_freq_cmd_145m.hex"));
    auto p = civ::parseFrame(raw);
    REQUIRE(p.has_value());
    CHECK(p->cmd == 0x05);
    CHECK(toHex(p->payload) == "0000004501");
  }
  SECTION("读模式应答 FM/滤波器1") {
    auto raw = radio::test::loadSingleFrame(fixture("read_mode_resp_fm.hex"));
    auto p = civ::parseFrame(raw);
    REQUIRE(p.has_value());
    auto mi = civ::parseModeResponse(*p);
    REQUIRE(mi.has_value());
    CHECK(mi->mode == civ::Mode::FM);
    CHECK(mi->filter == 1);
  }
  SECTION("S-meter 应答 S9") {
    auto raw = radio::test::loadSingleFrame(fixture("read_smeter_resp_s9.hex"));
    auto p = civ::parseFrame(raw);
    REQUIRE(p.has_value());
    auto sm = civ::parseSmeterResponse(*p);
    REQUIRE(sm.has_value());
    CHECK(*sm == 120);
  }
  SECTION("PTT 应答开/关") {
    auto on = civ::parseFrame(
        radio::test::loadSingleFrame(fixture("read_ptt_resp_on.hex")));
    auto off = civ::parseFrame(
        radio::test::loadSingleFrame(fixture("read_ptt_resp_off.hex")));
    REQUIRE(on.has_value());
    REQUIRE(off.has_value());
    CHECK(*civ::parsePttResponse(*on) == true);
    CHECK(*civ::parsePttResponse(*off) == false);
  }
  SECTION("PTT ON 命令帧") {
    auto raw = radio::test::loadSingleFrame(fixture("ptt_on_cmd.hex"));
    auto p = civ::parseFrame(raw);
    REQUIRE(p.has_value());
    CHECK(p->cmd == 0x1C);
    CHECK(toHex(p->payload) == "0001");
  }
}

// 锚点：120=S9=-73dBm，0=S0，241=S9+60dB
TEST_CASE("civ: att and sw16 switch frames", "[civ]") {
  CHECK(toHex(civ::attFrame(true, 0xA4, 0xE0)) == "fefea4e01120fd");
  CHECK(toHex(civ::attFrame(false, 0xA4, 0xE0)) == "fefea4e01100fd");
  CHECK(toHex(civ::readAttFrame(0xA4, 0xE0)) == "fefea4e011fd");
  CHECK(toHex(civ::sw16SetFrame(civ::kSub16Nb, 0x01, 0xA4, 0xE0)) ==
        "fefea4e0162201fd");
  CHECK(toHex(civ::sw16ReadFrame(civ::kSub16Agc, 0xA4, 0xE0)) ==
        "fefea4e01612fd");

  // 应答解析
  auto mk = [](uint8_t cmd, std::span<const uint8_t> payload) {
    civ::CivFrame f;
    f.to = 0xE0; f.from = 0xA4; f.cmd = cmd;
    f.payload.assign(payload.begin(), payload.end());
    return f;
  };
  const uint8_t att_on[] = {0x20};
  CHECK(*civ::parseAttResponse(mk(0x11, att_on)) == 0x20);
  const uint8_t nb_on[] = {0x22, 0x01};
  CHECK(*civ::parseSw16Response(mk(0x16, nb_on), 0x22) == 1);
  CHECK_FALSE(civ::parseSw16Response(mk(0x16, nb_on), 0x40).has_value());
}

TEST_CASE("civ: smeter conversion anchors", "[civ]") {
  CHECK(civ::smeterToDbm(120) == Approx(-73.0f).margin(0.01f));
  CHECK(civ::smeterToDbm(0) == Approx(-127.0f).margin(0.01f));
  CHECK(civ::smeterToDbm(241) == Approx(-13.0f).margin(0.01f));
  CHECK(civ::smeterSUnits(120) == Approx(9.0f).margin(0.001f));
}
