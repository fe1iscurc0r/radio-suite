// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <QJsonDocument>
#include <QJsonObject>

#include "apps/spike/scope_wire.h"

namespace {

QByteArray makeScopeFrame(uint32_t f_start, uint32_t f_end, uint16_t seq,
                          bool oor, const std::vector<uint8_t>& pixels) {
  QByteArray b;
  b.append(char(0x01));
  b.append(char(0x00));
  b.append(char(0x00));
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
  radio::spike::ScopeWireFrame f;
  REQUIRE(radio::spike::decodeScopeFrame(bin, &f));
  CHECK(f.receiver == 0);
  CHECK(f.mode == 0);
  CHECK(f.start_freq_hz == 14'000'000);
  CHECK(f.end_freq_hz == 14'500'000);
  CHECK(f.sequence == 0x1234);
  CHECK(f.out_of_range);
  CHECK(f.pixels == pixels);
}

TEST_CASE("engine: scope frame rejects short and wrong type", "[engine]") {
  radio::spike::ScopeWireFrame f;
  CHECK_FALSE(radio::spike::decodeScopeFrame(QByteArray(15, 0), &f));
  QByteArray wrong(32, 0);
  wrong[0] = char(0x02);
  CHECK_FALSE(radio::spike::decodeScopeFrame(wrong, &f));
  auto bad = makeScopeFrame(0, 0, 0, false, {0x01});
  bad[14] = 0x10;
  CHECK_FALSE(radio::spike::decodeScopeFrame(bad, &f));
}

TEST_CASE("engine: command envelope encoding", "[engine]") {
  const auto encoded = radio::spike::makeCommandEnvelope(
      "t1", "set_freq", QByteArrayLiteral("{\"freq\":14100000,\"receiver\":0}"));
  const auto doc = QJsonDocument::fromJson(encoded);
  REQUIRE(doc.isObject());
  const auto root = doc.object();
  CHECK(root.value("type").toString() == "cmd");
  CHECK(root.value("id").toString() == "t1");
  CHECK(root.value("name").toString() == "set_freq");
  REQUIRE(root.value("params").isObject());
  CHECK(root.value("params").toObject().value("freq").toInteger() == 14'100'000);
  CHECK(root.value("params").toObject().value("receiver").toInt() == 0);
}
