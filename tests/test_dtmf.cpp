// SPDX-License-Identifier: AGPL-3.0-or-later
// DTMF 编解码回环测试（16 键全覆盖）+ SSTV 编解码回环
#include <catch2/catch_test_macros.hpp>

#include "core/dtmf.h"
#include "core/sstv.h"

using namespace radio::core;

TEST_CASE("dtmf: freq table anchors (ITU Q.23)", "[dtmf]") {
  CHECK(dtmf::freqsOf('1').first == 697.0);
  CHECK(dtmf::freqsOf('1').second == 1209.0);
  CHECK(dtmf::freqsOf('0').first == 941.0);
  CHECK(dtmf::freqsOf('0').second == 1336.0);
  CHECK(dtmf::freqsOf('D').second == 1633.0);
  CHECK(dtmf::freqsOf('*').first == 941.0);
  CHECK(dtmf::isDigit('#'));
  CHECK_FALSE(dtmf::isDigit('E'));
}

TEST_CASE("dtmf: encode/decode roundtrip all 16 keys", "[dtmf]") {
  const std::string keys = "123A456B789C*0#D";
  const auto pcm = dtmf::encode(keys);
  const auto back = dtmf::decode(pcm);
  INFO("decoded: " << back);
  CHECK(back == keys);
}

TEST_CASE("dtmf: silence and noise yield nothing", "[dtmf]") {
  std::vector<int16_t> silence(44100, 0);
  CHECK(dtmf::decode(silence).empty());
  // 白噪（低幅度）
  std::vector<int16_t> noise(44100);
  uint32_t seed = 12345;
  for (auto& v : noise) {
    seed = seed * 1664525u + 1013904223u;
    v = static_cast<int16_t>((seed >> 16) % 3000);
  }
  // 噪声可能偶然触发个别键，但不应解出长序列
  CHECK(dtmf::decode(noise).size() <= 3);
}

TEST_CASE("sstv: encode/decode full roundtrip (grey image)", "[sstv]") {
  // 灰图：Y=128 → 解码应恢复接近的亮度（±16 容差，音频信道内的频率量化）
  std::vector<sstv::Rgb> img(320 * 240);
  for (auto& px : img) px = {128, 128, 128};
  const auto pcm = sstv::encodeRobot36(img, 44100);
  const auto back = sstv::decodeRobot36(pcm, 44100);
  REQUIRE(back.size() == img.size());
  long long sum = 0;
  for (size_t i = 0; i < back.size(); i += 997)  // 抽样约 7700 像素
    sum += (back[i].r + back[i].g + back[i].b) / 3;
  const long long n = 1 + (back.size() - 1) / 997;
  const double avg = double(sum) / n;
  CHECK(avg > 112.0);
  CHECK(avg < 144.0);
}

TEST_CASE("sstv: decode rejects input without VIS header", "[sstv]") {
  std::vector<int16_t> junk(44100 * 2, 1000);
  CHECK(sstv::decodeRobot36(junk, 44100).empty());
}
