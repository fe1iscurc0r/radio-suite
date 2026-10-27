// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/audio_bridge.h — 麦克风帧 → 50003 TX 的门禁队列
//
// 纯逻辑、无 Qt/无设备依赖，便于 CI 覆盖。规则来自安全门禁与半双工事实：
//   · 只有 PTT 相位 = Tx 才排队上行；进入/离开 Tx 都清空队列，
//     避免把按 PTT 之前的私语或切回 RX 后的残帧发上天。
//   · 队列有界（默认 10 帧 = 200ms），满则丢最旧并计数——实时语音宁缺勿积。
//   · TX 期间 RX 下行不播放（桥只回答 shouldPlayRx），防止扬声器回授。
#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <span>
#include <vector>

#include "core/audio_link.h"

namespace radio::app {

class AudioBridge {
 public:
  explicit AudioBridge(core::IAudioLink* link, size_t capacity_frames = 10)
      : link_(link), capacity_(capacity_frames ? capacity_frames : 1) {}

  void setTxActive(bool on) {
    std::lock_guard<std::mutex> lock(mu_);
    if (tx_active_ == on) return;
    tx_active_ = on;
    queue_.clear();
  }

  bool txActive() const {
    std::lock_guard<std::mutex> lock(mu_);
    return tx_active_;
  }

  bool shouldPlayRx() const {
    std::lock_guard<std::mutex> lock(mu_);
    return !tx_active_;
  }

  bool linkReady() const { return link_ && link_->audioReady(); }

  // 音频采集线程调用：非 TX 相位直接丢弃（麦克风常开，门禁在此）
  void pushMic(std::span<const int16_t> pcm20ms) {
    if (pcm20ms.empty()) return;
    std::lock_guard<std::mutex> lock(mu_);
    if (!tx_active_) return;
    if (queue_.size() >= capacity_) {
      queue_.pop_front();
      ++dropped_;
    }
    queue_.emplace_back(pcm20ms.begin(), pcm20ms.end());
  }

  // GUI 线程调用：把队列帧交给 50003；返回实际发送帧数
  size_t drain() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!tx_active_ || !linkReady()) {
      queue_.clear();
      return 0;
    }
    size_t sent = 0;
    while (!queue_.empty()) {
      if (!link_->sendTxAudio(queue_.front())) break;
      queue_.pop_front();
      ++sent;
    }
    return sent;
  }

  size_t queued() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
  }

  size_t dropped() const {
    std::lock_guard<std::mutex> lock(mu_);
    return dropped_;
  }

 private:
  core::IAudioLink* link_ = nullptr;
  size_t capacity_;
  mutable std::mutex mu_;
  bool tx_active_ = false;
  std::deque<std::vector<int16_t>> queue_;
  size_t dropped_ = 0;
};

}  // namespace radio::app
