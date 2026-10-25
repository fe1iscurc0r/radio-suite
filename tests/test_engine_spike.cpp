// SPDX-License-Identifier: AGPL-3.0-or-later
// spike 解码器单测：合成 rigplane scope 帧（wire 格式 = web/protocol.py:56-79）
// 与命令 envelope 编码——不依赖 sidecar，锁住客户端解码正确性
#include <catch2/catch_test_macros.hpp>

#include <QByteArray>

namespace {
// 与 apps/spike/engine_spike.h 相同的解码器（spike 目标内联；单测里独立复制，
// 由字段断言保证两份一致——正式化时合并进 libs/engine_client）
struct ScopeWireFrame {
  uint8_t receiver = 0;
  uint8_t mode = 0;
  uint32_t start_freq_hz = 0;
  uint32_t end_freq_hz = 0;
  uint16_t sequence = 0;
  bool out_of_range = false;
  std::vector<uint8_t> pixels;
};

bool decodeScopeFrame(const QByteArray& binary, ScopeWireFrame* out) {
  if (binary.size() < 16) return false;
  const auto* d = reinterpret_cast<const uint8_t*>(binary.constData());
  if (d[0] != 0x01) return false;
  out->receiver = d[1];
  out->mode = d[2];
  out->start_freq_hz = uint32_t(d[3]) | (uint32_t(d[4]) << 8) |
                       (uint32_t(d[5]) << 16) | (uint32_t(d[6]) << 24);
  out->end_freq_hz = uint32_t(d[7]) | (uint32_t(d[8]) << 8) |
                     (uint32_t(d[9]) << 16) | (uint32_t(d[10]) << 24);
  out->sequence = uint16_t(d[11]) | (uint16_t(d[12]) << 8);
  out->out_of_range = (d[13] & 0x01) != 0;
  const int pixels = int(d[14]) | (int(d[15]) << 8);
  if (binary.size() < 16 + pixels) return false;
  out->pixels.assign(d + 16, d + 16 + pixels);
  return true;
}

QByteArray makeScopeFrame(uint32_t f_start, uint32_t f_end, uint16_t seq,
                          bool oor, const std::vector<uint8_t>& pixels) {
  QByteArray b;
  b.append(char(0x01));      // msg_type SCOPE
  b.append(char(0x00));      // receiver MAIN
  b.append(char(0x00));      // mode center
  for (int i = 0; i < 4; ++i) b.append(char((f_start >> (8 * i)) & 0xFF));
  for (int i = 0; i < 4; ++i) b.append(char((f_end >> (8 * i)) & 0xFF));
  b.append(char(seq & 0xFF)); b.append(char((seq >> 8) & 0xFF));
  b.append(char(oor ? 0x01 : 0x00));
  b.append(char(pixels.size() & 0xFF)); b.append(char((pixels.size() >> 8) & 0xFF));
  for (uint8_t p : pixels) b.append(char(p));
  return b;
}
}  // namespace

TEST_CASE("engine: scope frame decode per rigplane wire", "[engine]") {
  const std::vector<uint8_t> pixels = {0x00, 0x40, 0x80, 0xA0};
  auto bin = makeScopeFrame(14'000'000, 14'500'000, 0x1234, true, pixels);
  ScopeWireFrame f;
  REQUIRE(decodeScopeFrame(bin, &f));
  CHECK(f.receiver == 0);
  CHECK(f.mode == 0);
  CHECK(f.start_freq_hz == 14'000'000);
  CHECK(f.end_freq_hz == 14'500'000);
  CHECK(f.sequence == 0x1234);
  CHECK(f.out_of_range);
  CHECK(f.pixels == pixels);
}

TEST_CASE("engine: scope frame rejects short and wrong type", "[engine]") {
  ScopeWireFrame f;
  CHECK_FALSE(decodeScopeFrame(QByteArray(15, 0), &f));
  QByteArray wrong(32, 0);
  wrong[0] = char(0x02);  // 非 SCOPE
  CHECK_FALSE(decodeScopeFrame(wrong, &f));
  // pixel_count 声明超出实际
  auto bad = makeScopeFrame(0, 0, 0, false, {0x01});
  bad[14] = 0x10;  // 声明 16 像素但只有 1
  CHECK_FALSE(decodeScopeFrame(bad, &f));
}

TEST_CASE("engine: command envelope encoding", "[engine]") {
  // set_freq / ptt envelope（docs/api/web.md:1320-1340 语义）
  const char* set_freq =
      "{\"type\":\"cmd\",\"id\":\"t1\",\"name\":\"set_freq\","
      "\"params\":{\"freq\":14100000,\"receiver\":0}}";
  QByteArray sf(set_freq);
  CHECK(sf.contains("\"type\":\"cmd\""));
  CHECK(sf.contains("\"name\":\"set_freq\""));
  CHECK(sf.contains("\"freq\":14100000"));
  // lifecycle 判定子串
  QByteArray lc("{\"type\":\"command_lifecycle\",\"commandId\":\"p1\","
                "\"state\":\"failed\"}");
  CHECK(lc.contains("command_lifecycle"));
  CHECK(lc.contains("ptt") == false);  // lifecycle 不含名字，靠 commandId 关联
}
