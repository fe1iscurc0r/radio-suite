// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/transport_udp.h — RS-BA1 V2 UDP 真机链路（IC-705 直连）
//
// 流程对齐 vendor/rsba1-core/src/rsba1/radio_link.py（IC-705 真机验证 2026-08-18）：
//   ctrl(50001) pkt3/4/6 → login → auth(02/05) → 等0xA8 → ConnectTrans
//   → serial(50002) pkt3/4/6 → serial open(magic=0x05) → CI-V 透传
//   close 五件套：serial close(0x00) → audio 拆除 → deauth(0x01) → 各信道 disconnect → 关 socket
//
// 端序定案：全信道统一 cmdTransportHeader（SID=BE）——真机 E2E 即此路径
// （serial_client.py 的全 LE 变体是另一条直连实现，见 docs/original-rsba1-feature-map.md）。
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/audio_link.h"
#include "core/audio_stream.h"
#include "core/rsba1_proto.h"
#include "core/transport.h"

class QUdpSocket;

namespace radio::core {

class Rsba1UdpTransport final : public ICivTransport, public IAudioLink {
 public:
  struct Config {
    std::string host;         // 电台 IP（RS-BA1 Server Function）
    std::string username;     // 电台侧 WLAN 远程设置的 RS-BA1 用户名/密码
    std::string password;
    uint16_t cmd_port = rsba1::kCommandPort;
    uint16_t ser_port = rsba1::kSerialPort;
    uint16_t aud_port = rsba1::kAudioPort;
    int handshake_timeout_ms = 3000;
  };

  explicit Rsba1UdpTransport(Config cfg);
  ~Rsba1UdpTransport() override;

  bool open(std::string* error) override;
  void close() override;
  bool isOpen() const override { return state_ == State::Connected; }
  bool send(std::span<const uint8_t> frame) override;
  size_t poll() override;

  // 诊断：最近一次建链各阶段日志（UI 状态栏呈现）
  const std::string& lastLog() const { return log_; }

  // ---- IAudioLink（50003；app 层负责 PTT 门禁与设备播放）----
  bool audioReady() const override { return audio_ready_; }
  void setRxAudioCallback(RxCallback cb) override { audio_cb_ = std::move(cb); }
  bool sendTxAudio(std::span<const int16_t> pcm) override;

 private:
  enum class State { Idle, CtrlHandshake, Login, SerialHandshake, Connected, Failed };

  bool handshake(QUdpSocket& sock, uint32_t& local_sid, uint32_t& remote_sid,
                 const char* name, std::string* error);
  // 等待一个满足 pred 的包（消费期间顺带处理 serial 噪声包）
  std::vector<uint8_t> waitPacket(QUdpSocket& sock, int timeout_ms,
                                  bool (*pred)(const std::vector<uint8_t>&),
                                  const char* what);

 private:
  IAudioLink::RxCallback audio_cb_;
  rsba1::AudioRxBuffer audio_rx_;
  rsba1::AudioTxPacker audio_tx_;
  bool audio_ready_ = false;
  std::vector<int16_t> audio_scratch_;  // 回调 span 的对齐缓冲（UDP 载荷按字节到达）

 public:  // 原有接口区（保持可读）
  void dispatchCtrl(std::vector<uint8_t> data);
  void dispatchSerial(std::vector<uint8_t> data);
  void log(const std::string& line);

  Config cfg_;
  State state_ = State::Idle;
  QUdpSocket* ctrl_ = nullptr;  // 50001
  QUdpSocket* ser_ = nullptr;   // 50002
  QUdpSocket* aud_ = nullptr;   // 50003（申请并拆除，防电台 MOD 挂死；流本体 M2）

  uint32_t ctrl_local_ = 0, ctrl_remote_ = 0;
  uint32_t ser_local_ = 0, ser_remote_ = 0;
  uint32_t aud_local_ = 0, aud_remote_ = 0;
  std::array<uint8_t, 6> auth_id_{};
  uint16_t ctrl_seq_ = 0;   // cmd 传输头 seq（发送侧递增）
  uint16_t ser_seq_ = 0;    // serial 信道 seq
  uint16_t sseq_ = 0;       // Serial 帧 sseq
  bool serial_opened_ = false;
  std::map<uint16_t, std::vector<uint8_t>> ser_tx_buf_;  // 重传缓存
  int64_t last_reauth_ms_ = 0;
  size_t delivered_ = 0;
  std::string log_;
};

}  // namespace radio::core
