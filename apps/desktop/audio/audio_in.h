// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · audio/audio_in.h — 麦克风采集（桌面 RtAudio / Android AAudio，48kHz s16le mono）
// 输出 20ms 帧（1920B）给 TX PCM 路径（sendAudioFrame / SSTV / DV）。
// 采集与发射的联动由调用方编排（PTT 门禁在 session/safety，此处只产帧）。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace radio::audio {

// 与 kappanhang/rigplane TX 线格式对齐：48kHz/16bit/mono/20ms = 1920B
inline constexpr uint32_t kTxCaptureRate = 48000;
inline constexpr size_t kTxFrameBytes = 1920;
inline constexpr size_t kTxFrameSamples = 960;  // 20ms

class AudioIn {
 public:
  using FrameCallback = std::function<void(std::span<const int16_t>)>;  // 960 样本

  AudioIn();
  ~AudioIn();

  std::vector<std::string> deviceNames() const;
  // 有输入通道的设备 ID 列表（与 deviceNames 顺序一致）——WASAPI ID 非连续
  std::vector<uint32_t> inputDeviceIds() const;
  // 系统默认输入设备 ID；-1 = 无可用设备
  int defaultInputDevice() const;

  // 开始采集；每凑满 20ms 触发一次 cb（采集线程内回调——消费方自行排队）
  bool start(uint32_t device_id, FrameCallback cb, std::string* error = nullptr);
  void stop();
  bool capturing() const { return capturing_; }
  // 采集溢出次数（本块仍上抛，仅用于诊断丢样）
  size_t overflows() const { return overflows_.load(std::memory_order_relaxed); }

 private:
  void* dac_ = nullptr;   // RtAudio* 或 AAudioStream*（PIMPL，避免头文件传染）
  bool capturing_ = false;
  void* ctx_ = nullptr;   // CaptureCtx
  std::atomic<size_t> overflows_{0};
};

}  // namespace radio::audio
