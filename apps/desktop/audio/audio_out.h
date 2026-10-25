// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · audio/audio_out.h — 音频输出（RtAudio/WASAPI）：SSTV/DTMF .wav 播放
// = "录音盒放录音"的最后一公里；发射联动由调用方（session 的 PTT 门禁）编排。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace radio::audio {

class AudioOut {
 public:
  AudioOut();
  ~AudioOut();

  // 列出输出设备（UI 选择用）
  std::vector<std::string> deviceNames() const;

  // 播放 16bit 单声道 PCM（阻塞式返回 false = 设备打开失败）
  bool play(const std::vector<int16_t>& pcm, uint32_t sample_rate,
            uint32_t device_id = 0, std::string* error = nullptr);
  void stop();

  bool playing() const { return playing_; }

 private:
  void* dac_ = nullptr;   // RtAudio*（PIMPL，避免头文件传染）
  bool playing_ = false;
};

}  // namespace radio::audio
