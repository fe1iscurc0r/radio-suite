// SPDX-License-Identifier: AGPL-3.0-or-later
// RS-BA1 UDP 协议层测试：与 Python 参考实现（vendor/rsba1-core）逐字节互锁
// fixtures 由 tests/fixtures/gen_from_reference.py 生成
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/civ.h"
#include "core/rsba1_proto.h"
#include "hex_util.h"

using namespace radio::core;
namespace r = radio::core::rsba1;

namespace {
std::string fixture(const std::string& name) {
  return std::string(RADIO_SUITE_FIXTURES_DIR) + "/" + name;
}
std::vector<uint8_t> fx(const std::string& name) {
  return radio::test::loadSingleFrame(fixture(name));
}
}  // namespace

constexpr uint32_t kLocal = 0x2A94BC02;
constexpr uint32_t kRemote = 0x19F8B4F7;
const std::array<uint8_t, 6> kAuthId = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

TEST_CASE("rsba1: passcode table matches reference", "[rsba1]") {
  auto pc = r::passcode("linnan");
  CHECK(radio::test::toHex(pc) ==
        radio::test::toHex(radio::test::loadSingleFrame(fixture("udp_passcode_linnan.hex"))));
  auto pa = r::passcode("a");
  CHECK(radio::test::toHex(pa) ==
        radio::test::toHex(radio::test::loadSingleFrame(fixture("udp_passcode_a.hex"))));
  CHECK(pa[0] == 0x38);  // 'a'(0x61)+0 → table[0x61-32]=0x38
}

TEST_CASE("rsba1: transport ctrl packets byte-exact", "[rsba1]") {
  CHECK(radio::test::toHex(r::buildPkt3(kLocal)) ==
        radio::test::toHex(fx("udp_pkt3.hex")));
  CHECK(radio::test::toHex(r::buildPkt6(kLocal, kRemote)) ==
        radio::test::toHex(fx("udp_pkt6.hex")));
  CHECK(radio::test::toHex(r::buildDisconnect(kLocal, kRemote)) ==
        radio::test::toHex(fx("udp_disconnect.hex")));
  // 传输头端序自检：len/type/seq LE，SID BE
  auto pkt3 = r::buildPkt3(kLocal);
  REQUIRE(pkt3.size() == 16);
  CHECK(pkt3[0] == 0x10);   // totalLen LE = 16
  CHECK(pkt3[4] == 0x03);   // type LE
  CHECK(pkt3[8] == 0x2A);   // localSID BE：0x2A94BC02 → 2A 94 BC 02
  CHECK(pkt3[9] == 0x94);
  CHECK(pkt3[10] == 0xBC);
  CHECK(pkt3[11] == 0x02);
}

TEST_CASE("rsba1: login/auth/connectTrans byte-exact", "[rsba1]") {
  const std::array<uint8_t, 2> aid = {0xAB, 0xCD};
  auto login = r::buildLoginRequest("linnan", "test123", kLocal, kRemote, 1, 0, aid);
  REQUIRE(login.size() == 0x80);
  CHECK(radio::test::toHex(login) == radio::test::toHex(fx("udp_login.hex")));

  auto a2 = r::buildAuthRequest(0x02, kLocal, kRemote, 2, 0x12, kAuthId);
  REQUIRE(a2.size() == 0x40);
  CHECK(radio::test::toHex(a2) == radio::test::toHex(fx("udp_auth02.hex")));

  auto a5 = r::buildAuthRequest(0x05, kLocal, kRemote, 3, 0x15, kAuthId);
  CHECK(radio::test::toHex(a5) == radio::test::toHex(fx("udp_auth05.hex")));

  std::array<uint8_t, 16> a8{};
  for (size_t i = 0; i < 16; ++i) a8[i] = static_cast<uint8_t>(0xA0 + i);
  r::ConnectTransParams p;
  p.local_sid = kLocal;
  p.remote_sid = kRemote;
  p.outer_seq = 4;
  p.inner_seq = 0x30;
  p.auth_id6 = kAuthId;
  p.a8_reply_id16 = a8;
  p.username = "linnan";
  auto ct = r::buildConnectTransRequest(p);
  REQUIRE(ct.size() == 0x90);
  CHECK(radio::test::toHex(ct) == radio::test::toHex(fx("udp_connecttrans.hex")));
}

TEST_CASE("rsba1: response parsers", "[rsba1]") {
  auto ok = r::parseLoginResponse(fx("udp_login_resp_ok.hex"));
  REQUIRE(ok.has_value());
  CHECK(ok->ok);
  CHECK(ok->result_code == 0);
  CHECK(ok->auth_id == kAuthId);

  auto bad = r::parseLoginResponse(fx("udp_login_resp_badpass.hex"));
  REQUIRE(bad.has_value());
  CHECK_FALSE(bad->ok);
  CHECK(bad->result_code == -2);

  auto ctr = r::parseConnectTransResponse(fx("udp_connecttrans_resp.hex"));
  REQUIRE(ctr.has_value());
  CHECK(ctr->ok);
  CHECK(ctr->new_remote_sid == 0x11111111);
  CHECK(ctr->new_local_sid == 0x22222222);
  CHECK(ctr->dev_name == "IC-705");
}

TEST_CASE("rsba1: serial frame codec", "[rsba1]") {
  // CI-V 读频率帧打包（sseq=7 bulk）
  auto civ = civ::readFreqFrame(0xA4, 0xE0);
  auto sf = r::buildSerialFrame(civ, 7, true);
  CHECK(radio::test::toHex(sf) == radio::test::toHex(fx("udp_serial_frame_civ.hex")));
  auto back = r::parseSerialFrame(sf);
  REQUIRE(back.has_value());
  CHECK(back->bulk);
  CHECK(back->sseq == 7);
  CHECK(radio::test::toHex(back->payload) == radio::test::toHex(civ));

  // open 单字节控制帧（flags bit0=0）
  const uint8_t magic[] = {0x05};
  auto open = r::buildSerialFrame(magic, 0, false);
  CHECK(radio::test::toHex(open) == radio::test::toHex(fx("udp_serial_frame_open.hex")));
  CHECK(open[0] == 0xC0);
  auto ob = r::parseSerialFrame(open);
  REQUIRE(ob.has_value());
  CHECK_FALSE(ob->bulk);

  // frameLen 不符 → 拒绝
  std::vector<uint8_t> bad = open;
  bad[1] = 0x09;  // 声明 9 字节载荷
  CHECK_FALSE(r::parseSerialFrame(bad).has_value());
}

TEST_CASE("rsba1: wire header LE parser + echo semantics", "[rsba1]") {
  // serWireHeader 全 LE（仅 16B 头，payload_len 只填 totalLen）；
  auto w = r::serWireHeader(0x00, 0x1234, 0xAABBCCDD, 0x11223344, 4);
  REQUIRE(w.size() == 16);
  CHECK(w[0] == 20);  // totalLen = 16+4 LE
  auto p = r::parseSerWire(w);
  REQUIRE(p.has_value());
  CHECK(p->type == 0x00);
  CHECK(p->seq == 0x1234);
  CHECK(p->f8 == 0xAABBCCDD);
  CHECK(p->fc == 0x11223344);
}

TEST_CASE("rsba1: pkt7 keepalive detect and reply", "[rsba1]") {
  auto probe = r::buildPkt7(kLocal, kRemote, 5, std::nullopt);
  REQUIRE(probe.size() == 21);
  CHECK(r::isPkt7(probe));
  CHECK(probe[16] == 0x00);
  CHECK(probe[20] == 0x06);

  std::array<uint8_t, 4> rid{0xDE, 0xAD, 0xBE, 0xEF};
  auto reply = r::buildPkt7(kLocal, kRemote, 6, rid);
  CHECK(reply[16] == 0x01);
  CHECK((reply[17] == 0xDE && reply[18] == 0xAD && reply[19] == 0xBE && reply[20] == 0xEF));
}

TEST_CASE("rsba1: localSID derivation", "[rsba1]") {
  // 192.168.0.23 → BE 值 0xC0A80017；末两字节 0x0017，端口 50002 → 0x0017C352... 按定义
  CHECK(r::makeLocalSid(0xC0A80017, 50002) == ((0x0017u << 16) | 50002u));
}
