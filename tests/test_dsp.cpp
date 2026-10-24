// SPDX-License-Identifier: AGPL-3.0-or-later
// DSP 测试：FFT 精度 / 瀑布处理 / 合成信号（TEST_CASE 名保持 ASCII，理由见 test_civ.cpp 头注）
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "dsp/fft.h"
#include "dsp/synth.h"
#include "dsp/waterfall_frame.h"

using namespace radio::dsp;
using Catch::Approx;

TEST_CASE("dsp: single tone peaks at expected bin", "[dsp]") {
  constexpr size_t N = 2048;
  constexpr double Fs = 48'000.0;
  SpectrumAnalyzer an(N, Fs);
  SignalSynth synth(N, Fs);
  synth.setTones({{7'500.0, 0.5f}});
  synth.setNoiseFloor(0.0f);  // 无噪，便于定位

  std::vector<float> buf(N);
  synth.fill(buf.data());
  an.process(buf.data(), N);

  const auto& db = an.powerDb();
  REQUIRE(db.size() == N / 2 + 1);
  const size_t peak = static_cast<size_t>(
      std::max_element(db.begin(), db.end()) - db.begin());
  const double expect = 7'500.0 / an.binHz();
  CHECK(static_cast<double>(peak) == Approx(expect).margin(2.0));
}

TEST_CASE("dsp: bin-centered tone has exact peak and low far leakage", "[dsp]") {
  constexpr size_t N = 256;
  constexpr double Fs = 1'000.0;
  SpectrumAnalyzer an(N, Fs);
  constexpr size_t kBin = 32;
  std::vector<float> tone(N);
  // 相位取 0、对准 bin 32：x[n] = cos(2π·32·n/N)
  for (size_t n = 0; n < N; ++n)
    tone[n] = static_cast<float>(std::cos(2.0 * 3.14159265358979323846 *
                                          static_cast<double>(kBin * n) / N));

  an.process(tone.data(), N);
  const auto& db = an.powerDb();

  const size_t peak = static_cast<size_t>(
      std::max_element(db.begin(), db.end()) - db.begin());
  CHECK(peak == kBin);

  float away = -300.0f;
  for (size_t k = 1; k + 1 < db.size(); ++k) {
    if (k + 2 < kBin || k > kBin + 2) away = std::max(away, db[k]);
  }
  CHECK(db[kBin] - away > 30.0f);  // Hann 窗远端泄漏抑制
}

TEST_CASE("dsp: waterfall EMA / peak-hold / row bytes", "[dsp]") {
  constexpr size_t B = 64;
  WaterfallProcessor wf(B);
  wf.db_floor = -120.0f;
  wf.db_ceil = -20.0f;

  std::vector<float> flat(B, -80.0f);
  std::vector<uint8_t> row(B);
  for (int i = 0; i < 10; ++i) wf.process(flat.data(), B, row.data());

  // 平坦输入 → 行字节接近恒定且在映射范围内
  for (size_t i = 0; i < B; ++i) {
    CHECK(row[i] == row[0]);
    CHECK(row[0] == Approx(255.0f * 0.4f).epsilon(0.15));  // (-80+120)/100*255
  }

  // 突发强信号：EMA 抬升、峰值 ≥ 当前值
  std::vector<float> burst(B, -80.0f);
  burst[7] = -20.0f;
  wf.process(burst.data(), B, row.data());
  CHECK(wf.peakHoldDb()[7] >= wf.emaDb()[7]);
  CHECK(row[7] > row[8]);

  // 峰值按帧衰减
  const float p0 = wf.peakHoldDb()[7];
  wf.process(flat.data(), B, row.data());
  wf.process(flat.data(), B, row.data());
  CHECK(wf.peakHoldDb()[7] < p0);
}

TEST_CASE("dsp: synth silence is only bounded noise", "[dsp]") {
  constexpr size_t N = 512;
  SignalSynth synth(N, 48'000.0);
  synth.setTones({});
  synth.setNoiseFloor(1e-3f);
  std::vector<float> buf(N);
  synth.fill(buf.data());
  float peak = 0.0f;
  for (float v : buf) peak = std::max(peak, std::abs(v));
  CHECK(peak <= 4e-3f);
  CHECK(peak > 0.0f);
}
