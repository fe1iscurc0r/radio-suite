// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · audio/audio_out.h — 音频输出（RtAudio/WASAPI）：SSTV/DTMF/Voice Memory
// 播放 = "录音盒放录音"的最后一公里；发射联动由调用方（session 的 PTT 门禁）编排。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace radio::audio {

class AudioOut {
 public:
  AudioOut();
  ~AudioOut();

  // 默认输出设备 ID；-1 = 无可用设备（WASAPI ID 非连续，不能拿 0 当默认）
  int defaultOutputDevice() const;

  // 列出输出设备（UI 选择用）
  std::vector<std::string> deviceNames() const;

  // 播放 16bit 单声道 PCM（阻塞式；false = 设备打开失败）。stop() 可中断。
  bool play(const std::vector<int16_t>& pcm, uint32_t sample_rate,
            uint32_t device_id = 0, std::string* error = nullptr);

  // 非阻塞播放：内部工作线程执行，UI 线程立即返回。
  // done 在工作线程回调（调用方自行切回 GUI 线程）；busy() 为真时返回 false。
  bool playAsync(std::vector<int16_t> pcm, uint32_t sample_rate,
                 uint32_t device_id,
                 std::function<void(bool, std::string)> done = {});

  void stop();  // 请求中断（播放线程最多 50ms 内退出）
  void wait();  // stop + 等待工作线程结束（析构/销毁前调用）

  bool playing() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 常开输出流：RS-BA1 50003 下行语音 → 扬声器。
// push() 可从任意线程调用；队列有界（200ms），满则丢最旧保低延迟。
class AudioSink {
 public:
  AudioSink();
  ~AudioSink();
  AudioSink(const AudioSink&) = delete;
  AudioSink& operator=(const AudioSink&) = delete;

  // 默认输出设备 ID；-1 = 无可用设备（WASAPI ID 非连续，不能拿 0 当默认）
  int defaultOutputDevice() const;

  bool start(uint32_t device_id, uint32_t sample_rate = 48000,
             std::string* error = nullptr);
  void stop();
  bool running() const;

  void push(std::span<const int16_t> pcm);
  void flush();  // 丢弃积压（PTT 切换/半双工静音后不补播旧音频）

  size_t queuedSamples() const;
  size_t droppedSamples() const;
  size_t underruns() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace radio::audio
