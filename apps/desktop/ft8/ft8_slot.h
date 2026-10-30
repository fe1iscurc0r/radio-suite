// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · ft8/ft8_slot.h — 本机 FT8 槽解码器（无 Qt 依赖）
//
// 吃 RS-BA1 下行 RX PCM（48k s16 mono），按 15 s UTC 边界切槽、抽取到 12k，
// 槽满后在工作线程调 ft8::decode，结果回调给调用方（调用方自行切回 GUI 线程）。
// 不依赖 WSJT-X 进程：这是「本机解码」路径。
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "ft8/ft8_codec.h"

namespace radio::ft8 {

inline constexpr int64_t kSlotMs = 15'000;             // FT8 时隙 15 s
inline constexpr size_t kSlotSamples12k = 180'000;     // 12 kHz × 15 s

struct SlotResult {
  int64_t utc_ms = 0;   // 槽起点（UTC 毫秒）
  std::vector<DecodedMessage> messages;
  double decode_ms = 0.0;  // 本次槽解码耗时（诊断用）
};

class SlotDecoder {
 public:
  using Callback = std::function<void(const SlotResult&)>;

  // in_rate_hz 必须是 12 kHz 的整数倍（48k 为标准路径）
  explicit SlotDecoder(uint32_t in_rate_hz = 48000);
  ~SlotDecoder();
  SlotDecoder(const SlotDecoder&) = delete;
  SlotDecoder& operator=(const SlotDecoder&) = delete;

  // 解码线程回调（解码线程上调）；对象析构前须保证回调不触碰已销毁对象
  void setCallback(Callback cb);

  // 从 utc_now_ms 起对齐到下一个 15 s 边界开始收样本；返回对齐等待毫秒数
  int64_t start(int64_t utc_now_ms);
  void stop();
  bool running() const { return running_.load(std::memory_order_relaxed); }

  // 音频线程/GUI 线程喂 48k s16 mono（内部抽取到 12k）
  void push(const int16_t* samples, size_t count);
  void push(std::span<const int16_t> pcm) { push(pcm.data(), pcm.size()); }

  // 诊断：当前槽完成度 0..1、是否正在解码、丢弃的槽数（解码跟不上）
  double slotProgress() const;
  bool decoding() const { return decoding_.load(std::memory_order_relaxed); }
  size_t droppedSlots() const { return dropped_.load(std::memory_order_relaxed); }
  size_t slotsDecoded() const { return slots_done_.load(std::memory_order_relaxed); }
  // 启动后累计喂入的输入样本数：0 表示链路还没把音频送下来（区别于「在采」）
  size_t fedSamples() const { return fed_.load(std::memory_order_relaxed); }

 private:
  struct Job {
    uint64_t gen = 0;
    int64_t utc_ms = 0;
    std::vector<float> samples;
  };

  void workerLoop();
  void submitSlotLocked();

  const unsigned ratio_;         // 48000/12000 = 4
  const uint32_t in_rate_;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::thread worker_;
  bool shutdown_ = false;
  bool running_flag_ = false;    // mu_ 保护（running_ 是给外部的无锁快照）
  Callback callback_;
  std::vector<float> current_;   // 12k 样本，当前槽
  std::vector<int16_t> carry_;   // 抽取余数（< ratio_ 个输入样本）
  size_t skip_in_ = 0;           // 对齐到下个边界要丢掉的输入样本数
  int64_t slot_utc_ms_ = 0;      // 当前槽起点
  Job pending_;                  // 最多一槽在等解码
  uint64_t gen_ = 0;             // start/stop 使旧的在飞解码结果失效

  std::atomic<bool> running_{false};
  std::atomic<bool> decoding_{false};
  std::atomic<size_t> dropped_{0};
  std::atomic<size_t> slots_done_{0};
  std::atomic<size_t> fed_{0};
};

}  // namespace radio::ft8
