// SPDX-License-Identifier: AGPL-3.0-or-later
// APRS 协议核心测试：全格式解析 / 生成 / 过滤器 / AX.25+KISS / AFSK 回环 /
// 台站与消息存储 / iGate 策略。
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "aprs/afsk.h"
#include "aprs/aprs.h"
#include "aprs/igate.h"
#include "aprs/store.h"

using namespace radio::aprs;
using Catch::Approx;

namespace {
Packet mustParse(std::string_view line) {
  Packet p;
  std::string err;
  REQUIRE(parseTnc2(line, &p, &err));
  return p;
}
}  // namespace

TEST_CASE("aprs: passcode and SSID stripping", "[aprs]") {
  CHECK(passcode("N0CALL") == 13023);
  CHECK(passcode("N0CALL-9") == 13023);
  CHECK(passcode("n0call") == 13023);
  CHECK(passcode("") == (0x73e2 & 0x7fff));
}

TEST_CASE("aprs: uncompressed position", "[aprs]") {
  const auto p = mustParse("N0CALL>APRS,WIDE1-1:!4903.50N/07201.75W-Test");
  CHECK(p.source == "N0CALL");
  CHECK(p.dest == "APRS");
  CHECK(p.kind == PacketKind::Position);
  CHECK(p.hasPosition());
  CHECK(p.pos.lat == Approx(49.05833).margin(1e-4));
  CHECK(p.pos.lon == Approx(-72.02917).margin(1e-4));
  CHECK(p.symbol.table == '/');
  CHECK(p.symbol.code == '-');
  CHECK(p.comment == "Test");
  CHECK_FALSE(p.messaging_capable);
  CHECK(p.via == "WIDE1-1");
}

TEST_CASE("aprs: timestamped position", "[aprs]") {
  const auto p = mustParse("N0CALL>APRS:@092345z4903.50N/07201.75W>Test");
  CHECK(p.timestamp == "092345z");
  CHECK(p.messaging_capable);
  CHECK(p.pos.lat == Approx(49.05833).margin(1e-4));
  CHECK(p.symbol.code == '>');
}

TEST_CASE("aprs: compressed position roundtrip", "[aprs]") {
  const LatLon ll{31.2304, 121.4737};
  const std::string info =
      makePosition(ll, Symbol{'/', '>'}, "Compressed", true, true);
  Packet p;
  REQUIRE(parseInfo(info, &p));
  CHECK(p.compressed);
  CHECK(p.hasPosition());
  CHECK(p.pos.lat == Approx(ll.lat).margin(0.001));
  CHECK(p.pos.lon == Approx(ll.lon).margin(0.001));
  CHECK(p.symbol.code == '>');
  CHECK(p.comment == "Compressed");
}

TEST_CASE("aprs: Mic-E decode", "[aprs]") {
  const auto p = mustParse(
      "N1ZZN-9>T2SP0W:`c_Vm6hk/ \"49}Originl Mic-E");
  CHECK(p.mic_e);
  CHECK(p.kind == PacketKind::Position);
  CHECK(p.pos.lat == Approx(42.50117).margin(1e-3));
  CHECK(p.pos.lon == Approx(-71.12633).margin(1e-3));
  CHECK(p.symbol.table == '/');
  CHECK(p.symbol.code == 'k');
  CHECK(p.mic_e_status == MicEStatus::InService);
}

TEST_CASE("aprs: object and item", "[aprs]") {
  const auto obj = mustParse(
      "N0CALL>APRS:;LEADER   *092345z4903.50N/07201.75W>088/036");
  CHECK(obj.kind == PacketKind::Object);
  CHECK(obj.object_name == "LEADER");
  CHECK(obj.object_alive);
  CHECK(obj.hasPosition());
  CHECK(obj.symbol.code == '>');

  const auto item = mustParse("N0CALL>APRS:)AID#2!4903.50N/07201.75W-Test");
  CHECK(item.kind == PacketKind::Item);
  CHECK(item.object_name == "AID#2");
  CHECK(item.object_alive);
  CHECK(item.hasPosition());
  CHECK(item.comment == "Test");
}

TEST_CASE("aprs: message ack and line", "[aprs]") {
  const auto msg = mustParse("N0CALL>APRS::KB2ICI-14:Testing{123");
  CHECK(msg.kind == PacketKind::Message);
  CHECK(msg.message.addressee == "KB2ICI-14");
  CHECK(msg.message.text == "Testing");
  CHECK(msg.message.id == "123");

  const auto ack = mustParse("N0CALL>APRS::KB2ICI-14:ack123");
  CHECK(ack.message.ack);
  CHECK(ack.message.id == "123");

  const auto rej = mustParse("N0CALL>APRS::KB2ICI-14:rejABC");
  CHECK(rej.message.rej);
  CHECK(rej.message.id == "ABC");

  const auto line = mustParse("N0CALL>APRS::KB2ICI-14:}12:reply text");
  CHECK(line.message.line == 12);
  CHECK(line.message.text == "reply text");

  const std::string out = makeMessage("kb2ici-14", "Hello", "7");
  Packet q;
  REQUIRE(parseInfo(out, &q));
  CHECK(q.message.addressee == "KB2ICI-14");
  CHECK(q.message.text == "Hello");
  CHECK(q.message.id == "7");
}

TEST_CASE("aprs: weather telemetry status query", "[aprs]") {
  const auto wx = mustParse(
      "N0CALL>APRS:!4903.50N/07201.75W_220/004g010t043r000p000h50b10132");
  CHECK(wx.kind == PacketKind::Weather);
  CHECK(wx.weather.present);
  CHECK(wx.weather.wind_dir_deg == Approx(220));
  CHECK(wx.weather.wind_kt == Approx(4));
  CHECK(wx.weather.gust_kt == Approx(10));
  CHECK(wx.weather.temp_c == Approx((43.0 - 32.0) * 5.0 / 9.0));
  CHECK(wx.weather.humidity == 50);
  CHECK(wx.weather.pressure_hpa == Approx(1013.2));

  const auto tlm = mustParse("N0CALL>APRS:T#005,199,000,255,073,123,01101001");
  CHECK(tlm.kind == PacketKind::Telemetry);
  CHECK(tlm.telemetry.seq == 5);
  CHECK(tlm.telemetry.analog[0] == Approx(199));
  CHECK(tlm.telemetry.analog[4] == Approx(123));
  CHECK(tlm.telemetry.digital == 0x69);

  const auto st = mustParse("N0CALL>APRS:>Net control 145.500");
  CHECK(st.kind == PacketKind::Status);
  CHECK(st.status == "Net control 145.500");

  const auto q = mustParse("N0CALL>APRS:?APRS?");
  CHECK(q.kind == PacketKind::Query);
}

TEST_CASE("aprs: third party unwrap", "[aprs]") {
  const auto p = mustParse(
      "IGATE>APRS,TCPIP*:}N0CALL>APRS,WIDE1-1:!4903.50N/07201.75W-Test");
  CHECK(p.kind == PacketKind::ThirdParty);
  const auto inner = unwrapThirdParty(p);
  REQUIRE(inner.has_value());
  CHECK(inner->source == "N0CALL");
  CHECK(inner->kind == PacketKind::Position);
  CHECK(inner->pos.lat == Approx(49.05833).margin(1e-4));
}

TEST_CASE("aprs: geodesy and grid", "[aprs]") {
  const LatLon shanghai{31.2304, 121.4737};
  const LatLon beijing{39.9042, 116.4074};
  CHECK(distanceKm(shanghai, beijing) == Approx(1068).margin(15));
  CHECK(bearingDeg(shanghai, beijing) == Approx(336).margin(4));

  const std::string grid = latLonToMaidenhead(shanghai);
  CHECK(grid == "PM01RF");
  bool ok = false;
  const LatLon back = maidenheadToLatLon(grid, &ok);
  CHECK(ok);
  CHECK(back.lat == Approx(shanghai.lat).margin(0.05));
  CHECK(back.lon == Approx(shanghai.lon).margin(0.05));
}

TEST_CASE("aprs: coordinate format", "[aprs]") {
  CHECK(formatLatitude(31.2387) == "3114.32N");
  CHECK(formatLongitude(121.3654) == "12121.92E");
  CHECK(formatLatitude(-33.8651) == "3351.91S");
  double v = 0;
  CHECK(parseLatitude("3114.32N", &v));
  CHECK(v == Approx(31.2387).margin(1e-4));
  CHECK(parseLongitude("12121.92E", &v));
  CHECK(v == Approx(121.3653).margin(1e-4));
}

TEST_CASE("aprs: filter parsing", "[aprs]") {
  const std::vector<std::string> valid = {
      "r/31.23/121.47/50", "m/100",      "b/N0CALL/KB2ICI-14",
      "p/BA/BD",          "o/LEADER",    "t/poimqstunw",
      "g/APRS",           "u/APRS",      "a/40/-75/30/-70",
      "e/N0CALL",         "f/N0CALL/50", "d/WIDE1-1/WIDE2-2",
      "q/qAR,qAI",
  };
  for (const auto& text : valid) {
    FilterLine f;
    std::string err;
    INFO(text);
    CHECK(parseFilterLine(text, &f, &err));
    CHECK_FALSE(filterToString(f).empty());
  }

  FilterLine f;
  std::string err;
  CHECK_FALSE(parseFilterLine("r/33/", &f, &err));
  CHECK_FALSE(parseFilterLine("m/0", &f, &err));
  CHECK_FALSE(parseFilterLine("x/1", &f, &err));
  CHECK_FALSE(parseFilterLine("a/30/-70/40/-75", &f, &err));
  CHECK_FALSE(parseFilterLine("t/zzz", &f, &err));

  const std::vector<FilterLine> filters = {
      FilterLine{FilterKind::Range, "31.2300/121.4700/50"},
      FilterLine{FilterKind::MyRange, "100"},
      FilterLine{FilterKind::Type, "pom"},
  };
  CHECK(buildFilter(filters) == "r/31.2300/121.4700/50 m/100 t/pom");
}

TEST_CASE("aprs: AX.25 roundtrip", "[aprs]") {
  Ax25Frame f;
  f.dest.call = "APRS";
  f.source.call = "N0CALL";
  f.source.ssid = 9;
  f.via.push_back(Ax25Address{"WIDE1", 1, false, false});
  f.info = "!4903.50N/07201.75W-Test";

  const auto bytes = encodeAx25(f);
  Ax25Frame back;
  std::string err;
  REQUIRE(parseAx25(bytes, &back, &err));
  CHECK(back.dest.call == "APRS");
  CHECK(back.source.call == "N0CALL");
  CHECK(back.source.ssid == 9);
  REQUIRE(back.via.size() == 1);
  CHECK(back.via[0].call == "WIDE1");
  CHECK(back.via[0].ssid == 1);
  CHECK(back.info == f.info);
  CHECK(ax25ToTnc2(back) == "N0CALL-9>APRS,WIDE1-1:!4903.50N/07201.75W-Test");

  // 非 UI 帧被拒绝（dest 7 + source 7 + via 7 之后是控制字节）
  auto bad = bytes;
  bad[21] = 0x00;
  CHECK_FALSE(parseAx25(bad, &back, &err));
}

TEST_CASE("aprs: KISS framing", "[aprs]") {
  std::vector<uint8_t> payload = {0x01, 0xC0, 0x02, 0xDB, 0x03};
  const auto framed = kissEncode(payload, 0);
  REQUIRE(framed.front() == 0xC0);
  REQUIRE(framed.back() == 0xC0);
  REQUIRE(framed.size() == payload.size() + 5);  // port + 2 转义 + 首尾 FEND

  KissDecoder dec;
  for (size_t i = 0; i < framed.size(); ++i)
    dec.feed(std::span<const uint8_t>(&framed[i], 1));
  KissFrame f;
  REQUIRE(dec.pop(&f));
  CHECK(f.port == 0);
  CHECK(f.data == payload);
  CHECK_FALSE(dec.pop(&f));

  // 无端 FEND 的帧丢弃
  KissDecoder bad;
  const std::vector<uint8_t> junk = {0x01, 0x02, 0x03};
  bad.feed(junk);
  CHECK_FALSE(bad.pop(&f));
}

TEST_CASE("aprs: AFSK loopback", "[aprs]") {
  Ax25Frame f;
  f.dest.call = "APRS";
  f.source.call = "N0CALL";
  f.source.ssid = 9;
  f.via.push_back(Ax25Address{"WIDE1", 1, false, false});
  f.info = "!4903.50N/07201.75W-Test 1200 baud loopback";
  const auto frame = encodeAx25(f);

  AfskConfig cfg;
  cfg.sample_rate = 48000;
  const auto audio = afskModulate(frame, cfg, 24, 2);
  REQUIRE(audio.size() > 1000);

  std::vector<int16_t> pcm(audio.size());
  for (size_t i = 0; i < audio.size(); ++i)
    pcm[i] = static_cast<int16_t>(std::lround(audio[i] * 32767.0));

  AfskDemodulator demod(cfg);
  demod.feed(pcm);
  std::vector<uint8_t> out;
  INFO("frames=" << demod.frames() << " crc_errors=" << demod.crc_errors()
                 << " tones=" << demod.tones() << " bits=" << demod.bits()
                 << " flags=" << demod.flags());
  REQUIRE(demod.pop(&out));
  CHECK(out == frame);
}

TEST_CASE("aprs: station store", "[aprs]") {
  StationStore store(3, 8);
  Packet p;
  REQUIRE(parseTnc2(
      "N0CALL-9>APRS,WIDE1-1:!4903.50N/07201.75W-Test", &p));
  CHECK(store.ingest(p, 1000) == "N0CALL-9");
  const Station* st = store.find("n0call-9");
  REQUIRE(st != nullptr);
  CHECK(st->packets == 1);
  CHECK(st->hasPosition());
  CHECK(st->pos.lat == Approx(49.05833).margin(1e-4));
  CHECK(st->track.size() == 1);
  CHECK(st->comment == "Test");

  Packet p2;
  REQUIRE(parseTnc2(
      "N0CALL-9>APRS:!4904.00N/07202.00W-Moved", &p2));
  store.ingest(p2, 20'000);
  st = store.find("N0CALL-9");
  REQUIRE(st != nullptr);
  CHECK(st->packets == 2);
  CHECK(st->track.size() == 2);
  CHECK(st->comment == "Moved");

  // 同一位置重复包不增加轨迹点
  store.ingest(p2, 21'000);
  CHECK(store.find("N0CALL-9")->track.size() == 2);

  // 对象按对象名聚合，不覆盖上报台站
  Packet obj;
  REQUIRE(parseTnc2("N0CALL>APRS:;LEADER   *092345z4903.50N/07201.75W>x", &obj));
  CHECK(store.ingest(obj, 22'000) == "LEADER");
  CHECK(store.find("LEADER")->is_object);

  // 容量淘汰最旧
  for (int i = 0; i < 4; ++i) {
    Packet q;
    REQUIRE(parseTnc2("S" + std::to_string(i) + ">APRS:>status", &q));
    store.ingest(q, 30'000 + i);
  }
  CHECK(store.size() == 3);
  CHECK(store.find("N0CALL-9") == nullptr);
}

TEST_CASE("aprs: message store", "[aprs]") {
  MessageStore ms;
  Packet in;
  REQUIRE(parseTnc2("KB2ICI-14>APRS::N0CALL   :Hello there{42", &in));
  CHECK(ms.ingest(in, "N0CALL", 1000));
  CHECK(ms.unread() == 1);
  REQUIRE(ms.inbox().size() == 1);
  CHECK(ms.inbox()[0].from == "KB2ICI-14");
  CHECK(ms.inbox()[0].text == "Hello there");
  CHECK(ms.inbox()[0].id == "42");
  CHECK(ms.markRead(0));
  CHECK(ms.unread() == 0);

  // 发给别人的消息不入箱
  Packet other;
  REQUIRE(parseTnc2("KB2ICI-14>APRS::N1ABC    :not me", &other));
  CHECK_FALSE(ms.ingest(other, "N0CALL", 2000));

  const std::string id = ms.enqueue("KB2ICI-14", "Ping", 3000);
  CHECK(id == "000");
  REQUIRE(ms.outbox().size() == 1);
  auto due = ms.due(3000 + 29999, 30000, 3);
  CHECK(due.empty());
  due = ms.due(3000 + 30000, 30000, 3);
  REQUIRE(due.size() == 1);
  CHECK(due[0]->id == id);

  ms.noteTry(id, 33'000);
  CHECK(ms.findOutbox(id)->tries == 1);

  CHECK(ms.acknowledge(id, false));
  CHECK(ms.findOutbox(id)->acked);

  // ACK 包只更新发件箱
  Packet ack;
  REQUIRE(parseTnc2("KB2ICI-14>APRS::N0CALL   :ack000", &ack));
  CHECK_FALSE(ms.ingest(ack, "N0CALL", 40'000));
  CHECK(ms.inbox().size() == 1);
}

TEST_CASE("aprs: igate policy", "[aprs]") {
  Packet rf;
  REQUIRE(parseTnc2("N0CALL-9>APRS,WIDE1-1:!4903.50N/07201.75W-Test", &rf));
  CHECK(isRfPacket(rf));
  CHECK_FALSE(isIsPacket(rf));
  CHECK(toIsPacket(rf, "BG5ABC") ==
        "N0CALL-9>APRS,WIDE1-1,qAR,BG5ABC:!4903.50N/07201.75W-Test");

  Packet is;
  REQUIRE(parseTnc2("N0CALL-9>APRS,TCPIP*,qAS,BG5ABC:!4903.50N/07201.75W-Test", &is));
  CHECK(isIsPacket(is));
  CHECK_FALSE(isRfPacket(is));
  CHECK(toIsPacket(is, "BG5ABC") == is.raw_line);

  IgatePolicy policy;
  policy.callsign = "BG5ABC";
  policy.is_to_rf = true;
  policy.tx_path = "WIDE1-1";
  CHECK(mayGateToRf(is, policy));
  CHECK(toRfPacket(is, policy) ==
        "N0CALL-9>APRS,WIDE1-1:!4903.50N/07201.75W-Test");
  CHECK_FALSE(mayGateToRf(rf, policy));
  CHECK(toRfPacket(rf, policy).empty());

  CHECK(thirdPartyPacket(rf, "BG5ABC") ==
        "BG5ABC>APRS,TCPIP*:}N0CALL-9>APRS,WIDE1-1:!4903.50N/07201.75W-Test");

  DuplicateFilter dup(30, 16);
  CHECK_FALSE(dup.duplicate(dedupKey(rf), 1000));
  CHECK(dup.duplicate(dedupKey(rf), 1500));
  CHECK(dup.duplicate(dedupKey(rf), 5000));
  CHECK_FALSE(dup.duplicate(dedupKey(rf), 32'000));
  CHECK(dup.size() == 1);
}
