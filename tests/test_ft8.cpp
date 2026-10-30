// SPDX-License-Identifier: AGPL-3.0-or-later
// 本地 FT8 编解码：文本 → 音调 → GFSK 音频 → 解调 → 文本（离线自证，不接电台）
#include <catch2/catch_test_macros.hpp>

#include "ft8/ft8_codec.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// 确定性 LCG 噪声：避免随机种子导致用例偶发
void addNoise(std::vector<float>& audio, float amplitude, uint32_t seed) {
  for (float& s : audio) {
    seed = seed * 1664525u + 1013904223u;
    s += amplitude * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
  }
}

}  // namespace

TEST_CASE("ft8: text roundtrips through offline synthesize and decode", "[ft8]") {
  const std::string kText = "CQ BG5GXO PM01";

  const auto tones = radio::ft8::encodeTones(kText);
  REQUIRE(tones.size() == static_cast<size_t>(radio::ft8::kTones));
  for (uint8_t t : tones) CHECK(t <= 7);

  auto audio = radio::ft8::synthesize(tones, 1000.0f);
  REQUIRE(audio.size() == 180000);  // 15 s @ 12 kHz
  addNoise(audio, 0.15f, 0x12345678u);

  const auto decoded = radio::ft8::decode(audio.data(), audio.size());
  bool found = false;
  for (const auto& m : decoded)
    if (m.text == kText) found = true;
  INFO("decoded " << decoded.size() << " messages, first = "
                  << (decoded.empty() ? std::string("<none>") : decoded.front().text));
  CHECK(found);
}

TEST_CASE("ft8: empty slot decodes nothing", "[ft8]") {
  std::vector<float> silence(180000, 0.0f);
  addNoise(silence, 0.01f, 0xfeedbeefu);
  CHECK(radio::ft8::decode(silence.data(), silence.size()).empty());
}

TEST_CASE("ft8: invalid text is rejected at encode time", "[ft8]") {
  CHECK_THROWS(radio::ft8::encodeTones("this text is far too long for ft8"));
}
