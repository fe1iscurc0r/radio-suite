// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/network_audio.h"

#include "app/session.h"

namespace radio::app {

NetworkAudio::NetworkAudio(RadioSession* session, QObject* parent)
    : QObject(parent),
      session_(session),
      bridge_(session ? session->audioLink() : nullptr) {
  pump_timer_.setInterval(10);  // 音频包 20ms 一帧，10ms 泵保证不攒批
  connect(&pump_timer_, &QTimer::timeout, this, &NetworkAudio::onPump);
}

NetworkAudio::~NetworkAudio() { stop(); }

bool NetworkAudio::start(std::string* error) {
  if (active_) return true;
  auto set_err = [error](const char* what) {
    if (error) *error = what;
    return false;
  };
  if (!session_) return set_err("无会话：请先连接电台");
  auto* link = session_->audioLink();
  if (!link)
    return set_err("当前链路无 50003 音频（mock / Python 桥不支持）");
  if (!link->audioReady())
    return set_err("50003 音频信道未握手：检查电台 WLAN 远程设置与 UDP 通路");

  const int out_id = out_dev_ >= 0 ? out_dev_ : sink_.defaultOutputDevice();
  if (out_id < 0) return set_err("未找到音频输出设备");
  if (!sink_.start(static_cast<uint32_t>(out_id), core::kNetAudioRate, error))
    return false;

  const int in_id = in_dev_ >= 0 ? in_dev_ : mic_.defaultInputDevice();
  if (in_id < 0) {
    sink_.stop();
    return set_err("未找到麦克风输入设备");
  }
  if (!mic_.start(
          static_cast<uint32_t>(in_id),
          [this](std::span<const int16_t> pcm) { bridge_.pushMic(pcm); },
          error)) {
    sink_.stop();
    return false;
  }

  link->setRxAudioCallback(
      [this](std::span<const int16_t> pcm) { onRxPcm(pcm); });
  active_ = true;
  rx_chunks_ = 0;
  tx_frames_ = 0;
  pump_timer_.start();
  emit statusChanged(
      QString("网络音频 ON：50003 RX→扬声器 / 麦克风→PTT 上行（%1 kHz s16le）")
          .arg(core::kNetAudioRate / 1000));
  return true;
}

void NetworkAudio::stop() {
  if (!active_) return;
  active_ = false;
  pump_timer_.stop();
  mic_.stop();
  sink_.stop();
  if (auto* link = session_ ? session_->audioLink() : nullptr)
    link->setRxAudioCallback(nullptr);
  bridge_.setTxActive(false);
  emit statusChanged("网络音频 OFF");
}

void NetworkAudio::setTxActive(bool on) {
  bridge_.setTxActive(on);
  sink_.flush();  // 半双工：切相位时既不留 RX 积压，也不补播旧帧
}

void NetworkAudio::onPump() {
  session_->pump();
  tx_frames_ += bridge_.drain();
}

void NetworkAudio::onRxPcm(std::span<const int16_t> pcm) {
  ++rx_chunks_;
  if (bridge_.shouldPlayRx()) sink_.push(pcm);
  if (rx_tap_) rx_tap_(pcm);
}

AudioStats NetworkAudio::stats() {
  AudioStats s;
  s.rx_chunks = rx_chunks_;
  s.tx_frames = tx_frames_;
  s.tx_dropped = bridge_.dropped();
  s.sink_queued = sink_.queuedSamples();
  s.sink_dropped = sink_.droppedSamples();
  s.sink_underruns = sink_.underruns();
  s.mic_overflows = mic_.overflows();
  if (auto* link = session_ ? session_->audioLink() : nullptr) {
    const auto rs = link->rxStats();
    // 链路层块数含回调缺省时的计数，比本层回调次数更准；全零说明实现不支持
    if (rs.chunks != 0) s.rx_chunks = rs.chunks;
    s.rx_pending = rs.pending;
    s.rx_lost = rs.lost;
    s.rx_dropped = rs.dropped;
  }
  return s;
}

}  // namespace radio::app
