// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tests/test_wave_io.cpp — W3.2 录制/回放/计数（纯离线）
// 判据：WAV 往返逐字节一致（A-3.4 离线可跑）、采样率变异必须拒读（A-3.5）、
// 同一录制文件连跑 3 次解码结果逐条一致（A-3.3 回放幂等）。
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "dsp/wave_io.h"
#include "aprs/afsk.h"
#include "aprs/aprs.h"

namespace fs = std::filesystem;

namespace {

std::string tmpPath(const char* name) {
  return (fs::temp_directory_path() / name).string();
}

// 用既有调制器合成一段含一个 AX.25 帧的音频（不依赖电磁环境）
std::vector<int16_t> synthAfskAudio() {
  radio::aprs::Ax25Frame f;
  f.dest.call = "APRS";
  f.source.call = "N0CALL";
  f.source.ssid = 9;
  f.info = "!4903.50N/07201.75W-Test replay";
  const auto frame = radio::aprs::encodeAx25(f);
  const auto floats = radio::aprs::afskModulate(frame, {}, 32, 2);
  std::vector<int16_t> pcm(floats.size());
  for (size_t i = 0; i < floats.size(); ++i)
    pcm[i] = static_cast<int16_t>(std::lround(floats[i] * 32767.0f));
  return pcm;
}

}  // namespace

TEST_CASE("wave io: roundtrip byte-exact", "[dsp]") {
  const auto path = tmpPath("rs-wave-io-test.wav");
  const std::vector<int16_t> pcm = {1, -2, 3, 0, -32768, 32767, 100, -100};
  std::string err;
  REQUIRE(radio::dsp::writeWavMono16(path, pcm, 48000, &err));
  std::vector<int16_t> out;
  REQUIRE(radio::dsp::readWavMono16(path, 48000, &out, &err));
  CHECK(out == pcm);

  // A-3.5 变异：头里采样率改成 44100 → 必须拒读，绝不静默按 48k 硬解
  {
    std::ofstream f(path, std::ios::binary | std::ios::in);
    f.seekp(24);
    const uint8_t le[4] = {0x58, 0xAC, 0x00, 0x00};  // 44100 LE
    f.write(reinterpret_cast<const char*>(le), 4);
  }
  std::vector<int16_t> out2;
  CHECK_FALSE(radio::dsp::readWavMono16(path, 48000, &out2, &err));
  CHECK(err.find("采样率不符") != std::string::npos);
  fs::remove(fs::path(path));
}

TEST_CASE("wave io: afsk replay idempotent x3", "[dsp][aprs]") {
  const auto path = tmpPath("rs-wave-replay-test.wav");
  const std::vector<int16_t> pcm = synthAfskAudio();
  std::string err;
  REQUIRE(radio::dsp::writeWavMono16(path, pcm, 48000, &err));

  // 连跑 3 次：帧计数与帧内容逐条一致（A-3.3）
  std::vector<std::vector<uint8_t>> ref;
  for (int run = 0; run < 3; ++run) {
    std::vector<int16_t> file_pcm;
    REQUIRE(radio::dsp::readWavMono16(path, 48000, &file_pcm, &err));
    radio::aprs::AfskDemodulator dem;
    dem.feed(file_pcm);
    std::vector<uint8_t> f;
    std::vector<std::vector<uint8_t>> got;
    while (dem.pop(&f)) got.push_back(f);
    const auto st = dem.stats();
    if (run == 0) {
      ref = got;
      CHECK(ref.size() >= 1);
      CHECK(st.decode_ok >= 1);
    } else {
      CHECK(got == ref);
    }
  }
  fs::remove(fs::path(path));
}
