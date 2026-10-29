// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

#include "wsjtx/wsjtx.h"

using namespace radio::wsjtx;

TEST_CASE("wsjtx: heartbeat roundtrip and header", "[wsjtx]") {
  const auto wire = encode(makeHeartbeat("WSJT-X", "2.7.0", "abc123"));
  REQUIRE(wire.size() > 20);
  // magic 0xadbccbda / schema 3 大端
  CHECK(wire[0] == 0xad);
  CHECK(wire[1] == 0xbc);
  CHECK(wire[2] == 0xcb);
  CHECK(wire[3] == 0xda);
  CHECK(wire[7] == kSchema);
  CHECK(wire[11] == 0);  // Heartbeat = 0

  Message m;
  std::string err;
  REQUIRE(decode(wire, &m, &err));
  CHECK(err.empty());
  CHECK(m.type == MessageType::Heartbeat);
  CHECK(m.id == "WSJT-X");
  const auto* hb = std::get_if<Heartbeat>(&m.body);
  REQUIRE(hb != nullptr);
  CHECK(hb->max_schema == kSchema);
  CHECK(hb->version == "2.7.0");
  CHECK(hb->revision == "abc123");
}

TEST_CASE("wsjtx: status roundtrip", "[wsjtx]") {
  Status s;
  s.dial_frequency = 14'074'000;
  s.mode = "FT8";
  s.dx_call = "BG5GXO";
  s.report = "-12";
  s.tx_mode = "FT8";
  s.tx_enabled = true;
  s.transmitting = false;
  s.decoding = true;
  s.rx_df = 1735;
  s.tx_df = 1735;
  s.de_call = "N0CALL";
  s.de_grid = "PM01";
  s.dx_grid = "OM89";
  s.tx_watchdog = true;
  s.sub_mode = "8-GFSK";
  s.fast_mode = false;
  s.special_operation_mode = 0;
  s.frequency_tolerance = 20;
  s.tr_period = 15;
  s.configuration_name = "Default";
  s.tx_message = "CQ N0CALL PM01";

  const auto wire = encode(Message{MessageType::Status, "WSJT-X", s});
  Message m;
  REQUIRE(decode(wire, &m));
  CHECK(m.type == MessageType::Status);
  const auto* got = std::get_if<Status>(&m.body);
  REQUIRE(got != nullptr);
  CHECK(got->dial_frequency == 14'074'000);
  CHECK(got->mode == "FT8");
  CHECK(got->dx_call == "BG5GXO");
  CHECK(got->report == "-12");
  CHECK(got->tx_mode == "FT8");
  CHECK(got->tx_enabled);
  CHECK_FALSE(got->transmitting);
  CHECK(got->decoding);
  CHECK(got->rx_df == 1735);
  CHECK(got->tx_df == 1735);
  CHECK(got->de_call == "N0CALL");
  CHECK(got->de_grid == "PM01");
  CHECK(got->dx_grid == "OM89");
  CHECK(got->tx_watchdog);
  CHECK(got->sub_mode == "8-GFSK");
  CHECK_FALSE(got->fast_mode);
  CHECK(got->frequency_tolerance == 20);
  CHECK(got->tr_period == 15);
  CHECK(got->configuration_name == "Default");
  CHECK(got->tx_message == "CQ N0CALL PM01");
}

TEST_CASE("wsjtx: decode and reply roundtrip", "[wsjtx]") {
  Decode d;
  d.is_new = true;
  d.time_ms = 12'345;
  d.snr = -12;
  d.delta_time = -0.3;
  d.delta_frequency = 1500;
  d.mode = "FT8";
  d.message = "CQ BG5GXO PM01";
  d.low_confidence = false;
  d.off_air = true;

  const auto wire = encode(Message{MessageType::Decode, "WSJT-X", d});
  Message m;
  REQUIRE(decode(wire, &m));
  const auto* got = std::get_if<Decode>(&m.body);
  REQUIRE(got != nullptr);
  CHECK(got->is_new);
  CHECK(got->time_ms == 12'345);
  CHECK(got->snr == -12);
  CHECK(got->delta_time == -0.3);
  CHECK(got->delta_frequency == 1500);
  CHECK(got->mode == "FT8");
  CHECK(got->message == "CQ BG5GXO PM01");
  CHECK_FALSE(got->low_confidence);
  CHECK(got->off_air);

  const auto reply_wire = encode(makeReply("WSJT-X", d, 0x02));
  Message r;
  REQUIRE(decode(reply_wire, &r));
  CHECK(r.type == MessageType::Reply);
  const auto* reply = std::get_if<Reply>(&r.body);
  REQUIRE(reply != nullptr);
  CHECK(reply->time_ms == 12'345);
  CHECK(reply->snr == -12);
  CHECK(reply->delta_time == -0.3);
  CHECK(reply->delta_frequency == 1500);
  CHECK(reply->mode == "FT8");
  CHECK(reply->message == "CQ BG5GXO PM01");
  CHECK(reply->modifiers == 0x02);

  // 协议字段是有符号 qint32：负偏移（低于刻度频率）必须原样往返
  d.delta_frequency = -1500;
  const auto neg_wire = encode(Message{MessageType::Decode, "WSJT-X", d});
  Message nm;
  REQUIRE(decode(neg_wire, &nm));
  const auto* neg = std::get_if<Decode>(&nm.body);
  REQUIRE(neg != nullptr);
  CHECK(neg->delta_frequency == -1500);
}

TEST_CASE("wsjtx: string prefix counts bytes, matching QDataStream", "[wsjtx]") {
  // WSJT-X 的 QString 走 QDataStream：长度前缀是字节数（"WSJT-X" = 6 码元 = 0x0C 字节）。
  // 写成码元数会让对端只认出 "W\0S\0J\0"，本端也解不了真实对端的包。
  const auto wire = encode(makeHeartbeat("WSJT-X", "2.7.0", "abc123"));
  REQUIRE(wire.size() > 16);
  CHECK(wire[12] == 0x00);
  CHECK(wire[13] == 0x00);
  CHECK(wire[14] == 0x00);
  CHECK(wire[15] == 0x0c);
}

TEST_CASE("wsjtx: decodes a packet in real QDataStream wire format", "[wsjtx]") {
  // 由本机 Qt 6.8 QDataStream 按 WSJT-X schema 3 的 Decode 布局生成的字节：
  // magic / schema / type / id / is_new / time / snr / delta_time / delta_freq /
  // mode / message / low_confidence / off_air
  static const char* kHex =
      "adbccbda00000003000000020000000c00570053004a0054002d005801"
      "02b32980fffffff43fb999999999999a000006c7000000060046005400"
      "380000001c00430051002000420047003500470058004f00200050004d"
      "003000310000";
  std::vector<uint8_t> wire;
  const auto hex_val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
  };
  for (size_t i = 0; kHex[i] && kHex[i + 1]; i += 2) {
    wire.push_back(static_cast<uint8_t>(hex_val(kHex[i]) * 16 + hex_val(kHex[i + 1])));
  }
  REQUIRE(wire.size() == 93);

  Message m;
  std::string err;
  REQUIRE(decode(wire, &m, &err));
  CHECK(err.empty());
  CHECK(m.type == MessageType::Decode);
  CHECK(m.id == "WSJT-X");
  const auto* d = std::get_if<Decode>(&m.body);
  REQUIRE(d != nullptr);
  CHECK(d->is_new);
  CHECK(d->time_ms == 45'296'000);
  CHECK(d->snr == -12);
  CHECK(d->delta_time == 0.1);
  CHECK(d->delta_frequency == 1735);
  CHECK(d->mode == "FT8");
  CHECK(d->message == "CQ BG5GXO PM01");
  CHECK_FALSE(d->low_confidence);
  CHECK_FALSE(d->off_air);
}

TEST_CASE("wsjtx: qso logged datetime roundtrip", "[wsjtx]") {  QsoLogged q;
  q.date_time_off = DateTime::fromUtcMs(1'700'000'000'000);
  q.dx_call = "BG5GXO";
  q.dx_grid = "PM01";
  q.tx_frequency = 14'074'000;
  q.mode = "FT8";
  q.report_sent = "-10";
  q.report_received = "-12";
  q.tx_power = "5";
  q.comments = "test";
  q.name = "op";
  q.date_time_on = DateTime::fromUtcMs(1'700'000'120'000);
  q.operator_call = "N0CALL";
  q.my_call = "N0CALL";
  q.my_grid = "PM01";
  q.exchange_sent = "PM01";
  q.exchange_received = "OM89";
  q.adif_propagation = "F2";

  const auto wire = encode(Message{MessageType::QsoLogged, "WSJT-X", q});
  Message m;
  REQUIRE(decode(wire, &m));
  CHECK(m.type == MessageType::QsoLogged);
  const auto* got = std::get_if<QsoLogged>(&m.body);
  REQUIRE(got != nullptr);
  CHECK(got->date_time_off.utcMs() == 1'700'000'000'000);
  CHECK(got->date_time_on.utcMs() == 1'700'000'120'000);
  CHECK(got->dx_call == "BG5GXO");
  CHECK(got->tx_frequency == 14'074'000);
  CHECK(got->mode == "FT8");
  CHECK(got->report_sent == "-10");
  CHECK(got->report_received == "-12");
  CHECK(got->name == "op");
  CHECK(got->operator_call == "N0CALL");
  CHECK(got->my_grid == "PM01");
  CHECK(got->exchange_received == "OM89");
  CHECK(got->adif_propagation == "F2");
}

TEST_CASE("wsjtx: control messages roundtrip", "[wsjtx]") {
  {
    Message m;
    REQUIRE(decode(encode(makeHaltTx("WSJT-X", true)), &m));
    CHECK(m.type == MessageType::HaltTx);
    const auto* h = std::get_if<HaltTx>(&m.body);
    REQUIRE(h != nullptr);
    CHECK(h->auto_tx_only);
  }
  {
    Message m;
    REQUIRE(decode(encode(makeFreeText("WSJT-X", "HELLO", true)), &m));
    const auto* f = std::get_if<FreeText>(&m.body);
    REQUIRE(f != nullptr);
    CHECK(f->text == "HELLO");
    CHECK(f->send);
  }
  {
    Message m;
    REQUIRE(decode(encode(makeReplay("WSJT-X")), &m));
    CHECK(m.type == MessageType::Replay);
    CHECK(std::holds_alternative<Replay>(m.body));
  }
  {
    Message m;
    REQUIRE(decode(encode(makeLocation("WSJT-X", "PM01aa")), &m));
    const auto* l = std::get_if<Location>(&m.body);
    REQUIRE(l != nullptr);
    CHECK(l->grid == "PM01aa");
  }
  {
    Message m;
    REQUIRE(decode(encode(makeClear("WSJT-X", 2)), &m));
    const auto* c = std::get_if<Clear>(&m.body);
    REQUIRE(c != nullptr);
    CHECK(c->window == 2);
  }
  {
    Message m;
    REQUIRE(decode(encode(makeClose("WSJT-X")), &m));
    CHECK(m.type == MessageType::Close);
  }
}

TEST_CASE("wsjtx: wspr, adif and configure roundtrip", "[wsjtx]") {
  WsprDecode w;
  w.is_new = false;
  w.time_ms = 45'000;
  w.snr = -20;
  w.delta_time = 0.5;
  w.frequency = 14'095'600;
  w.drift = 1;
  w.callsign = "BG5GXO";
  w.grid = "PM01";
  w.power_dbm = 37;
  w.off_air = false;
  {
    Message m;
    REQUIRE(decode(encode(Message{MessageType::WsprDecode, "WSJT-X", w}), &m));
    const auto* got = std::get_if<WsprDecode>(&m.body);
    REQUIRE(got != nullptr);
    CHECK_FALSE(got->is_new);
    CHECK(got->time_ms == 45'000);
    CHECK(got->snr == -20);
    CHECK(got->delta_time == 0.5);
    CHECK(got->frequency == 14'095'600);
    CHECK(got->drift == 1);
    CHECK(got->callsign == "BG5GXO");
    CHECK(got->power_dbm == 37);
  }
  {
    Message m;
    const std::string adif = "<adif_ver:5>3.0.7\n<EOH>\n<call:6>BG5GXO<EOR>";
    REQUIRE(decode(encode(Message{MessageType::LoggedAdif, "WSJT-X", LoggedAdif{adif}}),
                   &m));
    const auto* got = std::get_if<LoggedAdif>(&m.body);
    REQUIRE(got != nullptr);
    CHECK(got->adif == adif);
  }
  {
    Configure c;
    c.mode = "FT8";
    c.frequency_tolerance = 20;
    c.sub_mode = "8-GFSK";
    c.fast_mode = true;
    c.tr_period = 30;
    c.rx_df = 1800;
    c.dx_call = "BG5GXO";
    c.dx_grid = "PM01";
    c.generate_messages = true;
    Message m;
    REQUIRE(decode(encode(Message{MessageType::Configure, "WSJT-X", c}), &m));
    const auto* got = std::get_if<Configure>(&m.body);
    REQUIRE(got != nullptr);
    CHECK(got->mode == "FT8");
    CHECK(got->frequency_tolerance == 20);
    CHECK(got->sub_mode == "8-GFSK");
    CHECK(got->fast_mode);
    CHECK(got->tr_period == 30);
    CHECK(got->rx_df == 1800);
    CHECK(got->dx_call == "BG5GXO");
    CHECK(got->generate_messages);
  }
}

TEST_CASE("wsjtx: rejects truncated and foreign packets", "[wsjtx]") {
  const auto good = encode(makeHeartbeat("WSJT-X"));
  Message m;
  std::string err;
  CHECK_FALSE(decode(std::span<const uint8_t>(good).first(10), &m, &err));
  CHECK_FALSE(err.empty());

  auto corrupted = good;
  corrupted[0] = 0x00;
  CHECK_FALSE(decode(corrupted, &m, &err));

  const std::vector<uint8_t> foreign{1, 2, 3, 4, 5, 6};
  CHECK_FALSE(decode(foreign, &m, &err));

  CHECK_FALSE(decode(std::span<const uint8_t>{}, &m, &err));

  // 截断的 Decode（报文体少一半）必须失败
  Decode d;
  d.message = "CQ N0CALL";
  const auto decode_wire = encode(Message{MessageType::Decode, "WSJT-X", d});
  CHECK_FALSE(decode(std::span<const uint8_t>(decode_wire)
                         .first(decode_wire.size() - 5),
                     &m, &err));
}

TEST_CASE("wsjtx: utf8 strings use utf16 wire format", "[wsjtx]") {
  const std::string text = "测试 🛰";
  Message m;
  REQUIRE(decode(encode(makeFreeText("WSJT-X", text, false)), &m));
  const auto* f = std::get_if<FreeText>(&m.body);
  REQUIRE(f != nullptr);
  CHECK(f->text == text);

  // "A测" = 2 个 UTF-16 码元 = 4 字节，QDataStream 的长度字段是字节数（不是 UTF-8 字节数 5）
  const auto wire = encode(makeFreeText("X", "A测", false));
  // header: magic(4)+schema(4)+type(4)+id("X": 4+2)=18；FreeText 文本长度在偏移 18
  REQUIRE(wire.size() > 22);
  CHECK(wire[18] == 0);
  CHECK(wire[19] == 0);
  CHECK(wire[20] == 0);
  CHECK(wire[21] == 4);
}
