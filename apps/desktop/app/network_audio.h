// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/network_audio.h — RS-BA1 50003 端到端音频编排
//
// 把三类设备与链路缝在一起（全部在主线程编排）：
//   RX：transport.poll()（10ms 泵）→ AudioSink 常开输出 → 扬声器
//   TX：默认麦克风 20ms 帧 → AudioBridge（PTT 门禁）→ sendTxAudio 上行
//   tap：RX PCM 副本给频谱/瀑布，替换合成谱
// 半双工：TX 相位不播下行、也不补播积压；设备错误在 start() 一次性报出。
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <span>
#include <string>

#include "app/audio_bridge.h"
#include "audio/audio_in.h"
#include "audio/audio_out.h"

namespace radio::app {

class RadioSession;

class NetworkAudio : public QObject {
  Q_OBJECT
 public:
  explicit NetworkAudio(RadioSession* session, QObject* parent = nullptr);
  ~NetworkAudio() override;

  // 打开默认输出/输入设备并挂上 50003 回调；失败时 error 为可读原因
  bool start(std::string* error = nullptr);
  void stop();
  bool active() const { return active_; }

  // PTT 相位驱动（Tx 才允许麦克风上行；切相位时清空积压）
  void setTxActive(bool on);

  // 生成音频（AFSK 等）按 20 ms 块进同一门禁队列；需处于 Tx 相位
  void queueTxPcm(std::span<const int16_t> pcm) { bridge_.pushMic(pcm); }

  // RX PCM 副本（频谱源）；回调在 GUI 线程
  void setRxTap(std::function<void(std::span<const int16_t>)> cb) {
    rx_tap_ = std::move(cb);
  }

  size_t rxChunks() const { return rx_chunks_; }
  size_t txFrames() const { return tx_frames_; }
  size_t txDropped() const { return bridge_.dropped(); }
  size_t sinkQueued() const { return sink_.queuedSamples(); }

 signals:
  void statusChanged(const QString& text);

 private:
  void onPump();
  void onRxPcm(std::span<const int16_t> pcm);

  RadioSession* session_ = nullptr;
  audio::AudioIn mic_;
  audio::AudioSink sink_;
  AudioBridge bridge_;
  QTimer pump_timer_;
  std::function<void(std::span<const int16_t>)> rx_tap_;
  bool active_ = false;
  size_t rx_chunks_ = 0;
  size_t tx_frames_ = 0;
};

}  // namespace radio::app
