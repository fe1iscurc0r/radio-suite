// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tests/fake_rsba1_server.h — 本机环回用的假 RS-BA1 V2 电台
//
// 只被测试目标与台架工具 fake_rig 链接（不进产品二进制/APK：bench 开关默认 OFF），
// 模拟电台侧三信道：
//   50001 ctrl  ：pkt3/4/6、login、auth(0x02/0x05)、0xA8 下推、ConnectTrans、deauth
//   50002 serial：pkt3/4/6、serial open/close、CI-V 透传（双向）
//   50003 audio ：pkt3/4/6、20ms 帧的音频包（24B 头，kappanhang sendPart1/2 布局）
//
// 事实基准：
//   - login / ConnTrans 应答与 tests/fixtures/*.hex 逐字节一致；fixtures 由
//     rsba1-core 参考实现生成，IC-705 真机验证 2026-08-18。
//   - 音频包布局 = kappanhang audiostream.go sendPart1/2（MIT）。
//
// 线程模型：start() 起一条 std::thread，三张 QUdpSocket 全部在该线程创建与
// 使用；测试线程只经加锁队列注入、读快照，不跨线程碰 socket。
#pragma once

#include <QHostAddress>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

class QUdpSocket;

class FakeRsba1Server {
 public:
  FakeRsba1Server();
  ~FakeRsba1Server();
  FakeRsba1Server(const FakeRsba1Server&) = delete;
  FakeRsba1Server& operator=(const FakeRsba1Server&) = delete;

  bool start(std::string* error = nullptr);
  void stop();

  uint16_t ctrlPort() const { return ctrl_port_; }
  uint16_t serialPort() const { return serial_port_; }
  uint16_t audioPort() const { return audio_port_; }

  // ---- 应答参数（start() 时快照给服务线程；请在 start 前设置）----
  std::array<uint8_t, 6> auth_id = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  std::array<uint8_t, 16> a8_reply_id = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
                                         0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD,
                                         0xAE, 0xAF};
  uint32_t new_server_sid = 0x11111111;  // ConnectTrans 应答 [8:12]
  uint32_t new_client_sid = 0x22222222;  // ConnectTrans 应答 [12:16]
  std::string model = "IC-705";

  // ---- 台架参数（start 前设置；默认 = 既有环回随机端口行为，测试零改动）----
  // 事实基准不变：应答仍与 tests/fixtures/*.hex 逐字节一致。
  QHostAddress bind_address{QHostAddress::LocalHost};
  uint16_t ctrl_port = 0;    // 0 = 系统挑（既有测试行为）
  uint16_t serial_port = 0;
  uint16_t audio_port = 0;
  int handshake_delay_ms = 0;     // slow-handshake：login 应答前一次性拖 N ms
  int32_t login_result_code = 0;  // 非 0 = login 应答带拒绝结果码（auth-deny）

  // ---- 下行注入（下个服务循环发出）----
  void sendCiv(std::span<const uint8_t> frame);      // serial 信道，bulk 帧
  void sendAudio(std::span<const uint8_t> pcm, uint16_t seq);  // audio 信道

  // ---- 观测快照 ----
  std::vector<std::vector<uint8_t>> clientCivFrames();
  std::vector<std::vector<uint8_t>> clientAudioPackets();
  std::vector<std::vector<uint8_t>> clientAudioFrames();  // part1+part2 重组
  // 排水读（台架长跑防无界增长；测试用的快照读保持不变）
  std::vector<std::vector<uint8_t>> drainCivFrames();
  std::vector<std::vector<uint8_t>> drainAudioFrames();
  bool serialOpened();
  bool serialClosed();
  bool gotDeauth();
  int disconnectCount();

  // ---- 应答报文构造（静态；与 fixtures 逐字节对照用）----
  static std::vector<uint8_t> buildLoginReply(std::span<const uint8_t, 6> auth6,
                                              int32_t result_code = 0);
  static std::vector<uint8_t> buildAuthReply(uint8_t magic);
  static std::vector<uint8_t> buildA8Packet(std::span<const uint8_t, 16> reply16);
  static std::vector<uint8_t> buildConnectTransReply(uint32_t server_sid,
                                                    uint32_t client_sid,
                                                    std::span<const uint8_t, 6> auth6,
                                                    const std::string& model);
  // part2=false → 0x6c05 头（1364B 载荷）；true → 0x4402 头（556B）
  static std::vector<uint8_t> buildAudioPacket(std::span<const uint8_t> pcm,
                                               uint16_t seq, bool part2);

 private:
  enum class Kind { Ctrl, Serial, Audio };
  struct Chan {
    Kind kind = Kind::Ctrl;
    QUdpSocket* sock = nullptr;
    uint32_t server_sid = 0;
    uint32_t client_sid = 0;
    QHostAddress peer;
    quint16 peer_port = 0;
  };

  void pump(Chan& ch);
  void handlePacket(Chan& ch, const std::vector<uint8_t>& d);
  void handleControlPlane(Chan& ch, const std::vector<uint8_t>& d);
  void handleSerialPayload(const std::vector<uint8_t>& d);
  void handleAudioPayload(const std::vector<uint8_t>& d);
  void flushInjections();
  void send(Chan& ch, std::span<const uint8_t> data);
  void maybeHandshakeDelay();  // 服务线程内：slow-handshake 场景的一次性拖时

  std::thread thread_;
  std::atomic<bool> stop_{false};
  uint16_t ctrl_port_ = 0, serial_port_ = 0, audio_port_ = 0;

  std::mutex mu_;
  std::vector<std::vector<uint8_t>> inject_civ_;
  std::vector<std::vector<uint8_t>> inject_audio_;
  std::vector<std::vector<uint8_t>> civ_frames_;
  std::vector<std::vector<uint8_t>> audio_packets_;
  std::vector<std::vector<uint8_t>> audio_frames_;
  std::vector<uint8_t> audio_part1_;
  bool serial_opened_ = false;
  bool serial_closed_ = false;
  bool got_deauth_ = false;
  int disconnects_ = 0;
  uint16_t civ_sseq_ = 1;

  // 以下只在服务线程内使用（start() 之后、stop() 之前）
  Chan ctrl_, serial_, audio_;
  std::array<uint8_t, 6> run_auth_{};
  std::array<uint8_t, 16> run_a8_{};
  uint32_t run_server_sid_ = 0;
  uint32_t run_client_sid_ = 0;
  std::string run_model_;
  int run_handshake_delay_ms_ = 0;
  int32_t run_login_result_code_ = 0;
  bool handshake_delayed_ = false;  // 只在服务线程内读写
};
