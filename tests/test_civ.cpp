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

TEST_CASE("civ: dual-vfo frames and parsers", "[civ][vfo]") {
  // 指南 p.3/p.15：0x25/0x26 sub=00 选中 VFO、01 未选中 VFO
  CHECK(toHex(civ::readVfoFreqFrame(false, 0xA4, 0xE0)) == "fefea4e02500fd");
  CHECK(toHex(civ::readVfoFreqFrame(true, 0xA4, 0xE0)) == "fefea4e02501fd");
  CHECK(toHex(civ::setVfoFreqFrame(true, 7'074'000, 0xA4, 0xE0)) ==
        "fefea4e025010040070700fd");
  CHECK(toHex(civ::readVfoModeFrame(true, 0xA4, 0xE0)) == "fefea4e02601fd");
  CHECK(toHex(civ::setVfoModeFrame(true, civ::Mode::CW, 0, 1, 0xA4, 0xE0)) ==
        "fefea4e02601030001fd");
  CHECK(toHex(civ::selectVfoFrame(true, 0xA4, 0xE0)) == "fefea4e00701fd");
  CHECK(toHex(civ::swapVfoFrame(0xA4, 0xE0)) == "fefea4e0b0fd");
  CHECK(toHex(civ::equalizeVfoFrame(0xA4, 0xE0)) == "fefea4e0a0fd");

  auto mk = [](uint8_t cmd, std::span<const uint8_t> payload) {
    civ::CivFrame f;
    f.to = 0xE0; f.from = 0xA4; f.cmd = cmd;
    f.payload.assign(payload.begin(), payload.end());
    return f;
  };
  const uint8_t freq[] = {0x01, 0x00, 0x40, 0x07, 0x07, 0x00};
  auto vf = civ::parseVfoFreqResponse(mk(0x25, freq));
  REQUIRE(vf.has_value());
  CHECK(vf->unselected);
  CHECK(vf->hz == 7'074'000ULL);

  const uint8_t mode[] = {0x00, 0x03, 0x00, 0x02};
  auto vm = civ::parseVfoModeResponse(mk(0x26, mode));
  REQUIRE(vm.has_value());
  CHECK_FALSE(vm->unselected);
  CHECK(vm->mode == civ::Mode::CW);
  CHECK(vm->filter == 2);

  const uint8_t bad_sub[] = {0x02, 0x00, 0x40, 0x07, 0x07, 0x00};
  CHECK_FALSE(civ::parseVfoFreqResponse(mk(0x25, bad_sub)).has_value());
}

TEST_CASE("civ: cw text frames chunk and sanitize", "[civ][cw]") {
  auto one = civ::cwSendFrames("cq de k1abc", 0xA4, 0xE0);
  REQUIRE(one.size() == 1);
  CHECK(toHex(one[0]) == "fefea4e0174351204445204b31414243fd");

  auto long_text = civ::cwSendFrames(std::string(65, 'x'), 0xA4, 0xE0);
  REQUIRE(long_text.size() == 3);
  CHECK(long_text[0].size() == 36);  // FE FE to from cmd + 30B + FD
  CHECK(long_text[1].size() == 36);
  CHECK(long_text[2].size() == 11);  // 余 5 字节

  CHECK(civ::cwSendFrames("", 0xA4, 0xE0).empty());
  CHECK(toHex(civ::cwStopFrame(0xA4, 0xE0)) == "fefea4e017fffd");
}

TEST_CASE("civ: cw level conversions and 2B-BCD frames", "[civ][cw]") {
  CHECK(civ::wpmToKeySpeedLevel(6) == 0);
  CHECK(civ::wpmToKeySpeedLevel(48) == 255);
  CHECK(civ::wpmToKeySpeedLevel(20) == 85);
  CHECK(civ::keySpeedLevelToWpm(0) == 6);
  CHECK(civ::keySpeedLevelToWpm(255) == 48);
  CHECK(civ::keySpeedLevelToWpm(85) == 20);

  CHECK(civ::hzToCwPitchLevel(300) == 0);
  CHECK(civ::hzToCwPitchLevel(900) == 255);
  CHECK(civ::hzToCwPitchLevel(600) == 128);
  CHECK(civ::cwPitchLevelToHz(0) == 300);
  CHECK(civ::cwPitchLevelToHz(255) == 900);
  CHECK(civ::cwPitchLevelToHz(128) == 600);

  // 255 → 02 55（2B BCD MSB-first，与 radio_link.py 电平族一致）
  CHECK(toHex(civ::level14WriteFrame(civ::kSub14KeySpeed, 255, 0xA4, 0xE0)) ==
        "fefea4e0140c0255fd");
  CHECK(toHex(civ::level14ReadFrame(civ::kSub14CwPitch, 0xA4, 0xE0)) ==
        "fefea4e01409fd");
  CHECK(toHex(civ::breakInFrame(true, 0xA4, 0xE0)) == "fefea4e0164701fd");

  civ::CivFrame lv;
  lv.cmd = 0x14;
  lv.payload = {0x0C, 0x00, 0x55};
  CHECK(*civ::parseLevel14Response(lv, civ::kSub14KeySpeed) == 55);
  CHECK_FALSE(civ::parseLevel14Response(lv, civ::kSub14CwPitch).has_value());

  civ::CivFrame bi;
  bi.cmd = 0x16;
  bi.payload = {0x47, 0x01};
  CHECK(*civ::parseBreakInResponse(bi));
}

TEST_CASE("civ: menu item frames and response", "[civ][menu]") {
  // 1A 05 gg ii：[value...] 写；读不带 value（ic705.toml [menu]）
  CHECK(toHex(civ::menuReadFrame(0x00, 0x45, 0xA4, 0xE0)) ==
        "fefea4e01a050045fd");
  const uint8_t v[] = {0x01};
  CHECK(toHex(civ::menuWriteFrame(0x01, 0x31, v, 0xA4, 0xE0)) ==
        "fefea4e01a05013101fd");

  civ::CivFrame f;
  f.cmd = 0x1A;
  f.payload = {0x05, 0x03, 0x59, 0x20};
  auto mv = civ::parseMenuResponse(f);
  REQUIRE(mv.has_value());
  CHECK(mv->group == 3);
  CHECK(mv->item == 0x59);
  CHECK(mv->value == std::vector<uint8_t>{0x20});

  civ::CivFrame wrong;
  wrong.cmd = 0x1A;
  wrong.payload = {0x03, 0x00, 0x00};
  CHECK_FALSE(civ::parseMenuResponse(wrong).has_value());
}

TEST_CASE("civ: memory channel encode/decode", "[civ][memory]") {
  civ::MemoryChannel m;
  m.channel = 5;
  m.freq_hz = 7'050'000;
  m.mode = civ::Mode::LSB;
  m.filter = 1;
  m.name = "TEST";

  const std::vector<uint8_t> body = {
      0x1A, 0x00,              // cmd + sub
      0x00, 0x05,              // channel BCD2
      0x00,                    // scan
      0x00, 0x00, 0x05, 0x07, 0x00,  // freq 5B LSB-first
      0x00, 0x01,              // mode / filter（BCD1）
      0x00,                    // data<<4 | tone
      0x00, 0x00, 0x00,        // tone freq BCD3
      0x00, 0x00, 0x00,        // tsql freq BCD3
      'T', 'E', 'S', 'T', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // name 10B
      0x00};                   // 第 26 字节保留
  CHECK(civ::writeMemoryChannelFrame(m, 0xA4, 0xE0) ==
        civ::buildFrame(0xA4, 0xE0, body));
  CHECK(toHex(civ::readMemoryChannelFrame(5, 0xA4, 0xE0)) ==
        "fefea4e01a000005fd");
  CHECK(toHex(civ::selectMemoryGroupFrame(3, 0xA4, 0xE0)) ==
        "fefea4e008a00003fd");
  CHECK(toHex(civ::selectMemoryChannelFrame(7, 0xA4, 0xE0)) ==
        "fefea4e0080007fd");

  // 应答 payload：sub + channel(2) + 26 字节内容块（memory.py:parse_memory_contents_response）
  civ::CivFrame resp;
  resp.cmd = 0x1A;
  resp.payload = {0x00, 0x00, 0x05,  // sub + channel(2)
                  0x00,             // scan
                  0x00, 0x00, 0x05, 0x07, 0x00,
                  0x00, 0x01, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  'T', 'E', 'S', 'T', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00};
  auto got = civ::parseMemoryChannelResponse(resp);
  REQUIRE(got.has_value());
  CHECK(*got == m);

  civ::CivFrame short_resp;
  short_resp.cmd = 0x1A;
  short_resp.payload = {0x00, 0x00, 0x05, 0x00};
  CHECK_FALSE(civ::parseMemoryChannelResponse(short_resp).has_value());
}
