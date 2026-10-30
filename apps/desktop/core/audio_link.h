// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/audio_link.h — 网络音频链路能力（RS-BA1 UDP 50003）
//
// CI-V 传输（ICivTransport）只负责电台控制；语音通路单独抽象，便于
// mock/桥/未来其他链路各自决定是否具备音频能力：
//   RX：电台下行 PCM（48kHz s16le mono），由 poll() 内同步回调上抛
//   TX：客户端上行 20ms 帧（960 样本 / 1920B），全零帧由实现跳发
// 门禁（PTT 相位、半双工静音）不属于链路层，由 app 层编排。
#pragma once

#include <cstdint>
#include <functional>
#include <span>

namespace radio::core {

inline constexpr uint32_t kNetAudioRate = 48000;
inline constexpr size_t kNetAudioSamplesPerFrame = 960;  // 20ms
inline constexpr size_t kNetAudioFrameBytes = 1920;

class IAudioLink {
 public:
  using RxCallback = std::function<void(std::span<const int16_t>)>;

  // 50003 下行质量（UI 诊断用；不具备音频能力的链路保持全零）
  struct RxStats {
    uint64_t chunks = 0;   // 已按序上抛的块数
    uint32_t pending = 0;  // 重排窗内待发块
    uint32_t lost = 0;     // 缺口超时判丢块数
    uint32_t dropped = 0;  // 迟到/重复/超窗丢弃块数
  };

  virtual ~IAudioLink() = default;

  // 音频信道是否完成握手（链路连上但音频失败时为 false）
  virtual bool audioReady() const = 0;

  // 设置/清空 RX 回调（空 function 表示停止上抛）
  virtual void setRxAudioCallback(RxCallback cb) = 0;

  // 发送一帧 20ms PCM（960 样本）。长度不符/未就绪返回 false。
  virtual bool sendTxAudio(std::span<const int16_t> pcm) = 0;

  virtual RxStats rxStats() const { return {}; }
};

}  // namespace radio::core
