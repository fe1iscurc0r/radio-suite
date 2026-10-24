// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/rsba1_proto.h — RS-BA1 V2 UDP 协议编解码（纯协议层，无 socket）
//
// 事实基准：vendor/rsba1-core/src/rsba1/serial/{command_client,serial_codec,serial_client}.py
// （IC-705 真机验证 2026-08-18，MIT）。字节语义逐条对齐，fixtures 由 Python 参考生成
// 互锁（tests/fixtures/gen_from_reference.py）。
//
// 两层 16B 传输头，**SID 端序不同**（历史定案，勿"统一"）：
//   - Command 信道 (50001)：totalLen/type/seq LE + localSID/remoteSID **BE**
//   - Serial   信道 (50002)：totalLen/type/seq LE + field_8/field_C **全 LE**
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace radio::core::rsba1 {

// ---- 端口与会话 ----
inline constexpr uint16_t kCommandPort = 50001;
inline constexpr uint16_t kSerialPort = 50002;
inline constexpr uint16_t kAudioPort = 50003;

// localSID = (本地IP末两字节 << 16) | 本地端口
uint32_t makeLocalSid(uint32_t ip_be, uint16_t port);

// ICOM 凭证置换编码（passcode：码点+下标折回 32..126 查 95 项表，定长 16B）
std::array<uint8_t, 16> passcode(const std::string& text);

// ---- 传输头（两个信道）----
std::vector<uint8_t> cmdTransportHeader(uint32_t total_len, uint16_t type,
                                        uint16_t seq, uint32_t local_sid,
                                        uint32_t remote_sid);  // SID = BE
struct WireHeader {
  uint16_t type = 0;
  uint16_t seq = 0;
  uint32_t f8 = 0;   // localSID
  uint32_t fc = 0;   // remoteSID
};
std::optional<WireHeader> parseSerWire(std::span<const uint8_t> data);  // 全 LE
std::vector<uint8_t> serWireHeader(uint16_t type, uint16_t seq, uint32_t f8,
                                   uint32_t fc, size_t payload_len);

// ---- 传输层控制包（Command 信道头）----
std::vector<uint8_t> buildPkt3(uint32_t local_sid);
std::vector<uint8_t> buildPkt6(uint32_t local_sid, uint32_t remote_sid);
std::vector<uint8_t> buildDisconnect(uint32_t local_sid, uint32_t remote_sid);
std::vector<uint8_t> buildIdle0(uint32_t local_sid, uint32_t remote_sid,
                                uint16_t seq);
// 21B keepalive：flag 0x00 主动 / 0x01 应答回显 reply_id
std::vector<uint8_t> buildPkt7(uint32_t local_sid, uint32_t remote_sid,
                               uint16_t seq,
                               std::optional<std::array<uint8_t, 4>> reply_id,
                               uint16_t inner_seq = 0x8304);
bool isPkt7(std::span<const uint8_t> data);
bool isIdle0(std::span<const uint8_t> data);
bool isA8Packet(std::span<const uint8_t> data);
std::optional<std::array<uint8_t, 16>> extractA8ReplyId(std::span<const uint8_t> data);

// ---- 业务包（内层头 + 载荷，嵌于 cmd 传输头后）----
std::vector<uint8_t> buildLoginRequest(const std::string& username,
                                       const std::string& password,
                                       uint32_t local_sid, uint32_t remote_sid,
                                       uint16_t outer_seq, uint8_t inner_seq,
                                       std::array<uint8_t, 2> auth_start_id);
// magic: 0x02=GetInfo / 0x05=KeepAlive / 0x01=deauth
std::vector<uint8_t> buildAuthRequest(uint8_t magic, uint32_t local_sid,
                                      uint32_t remote_sid, uint16_t outer_seq,
                                      uint8_t inner_seq,
                                      std::span<const uint8_t> auth_id6);
struct ConnectTransParams {
  uint32_t local_sid, remote_sid;
  uint16_t outer_seq;
  uint8_t inner_seq;
  std::span<const uint8_t> auth_id6;
  std::span<const uint8_t> a8_reply_id16;
  std::string username;
  uint16_t serial_port = kSerialPort;
  uint16_t audio_port = kAudioPort;
  uint16_t sample_rate = 48000;
  uint16_t tx_seq_buf_ms = 300;
};
std::vector<uint8_t> buildConnectTransRequest(const ConnectTransParams& p);

// ---- 响应解析 ----
struct LoginResult {
  bool ok = false;
  std::array<uint8_t, 6> auth_id{};
  int32_t result_code = 0;  // 0=通过, -2=凭证错误
};
std::optional<LoginResult> parseLoginResponse(std::span<const uint8_t> data);
struct ConnectTransResult {
  bool ok = false;
  uint32_t new_remote_sid = 0;
  uint32_t new_local_sid = 0;
  std::array<uint8_t, 6> new_auth_id{};
  std::string dev_name;
};
std::optional<ConnectTransResult> parseConnectTransResponse(std::span<const uint8_t> data);
std::optional<uint8_t> parseAuthReplyMagic(std::span<const uint8_t> data);

// ---- Serial 帧（Serial 信道载荷：flags 0xC0|bit0 + frameLen LE + sseq BE）----
std::vector<uint8_t> buildSerialFrame(std::span<const uint8_t> payload,
                                       uint16_t sseq, bool bulk);
struct SerialFrame {
  bool bulk = true;
  uint16_t sseq = 0;
  std::vector<uint8_t> payload;
};
std::optional<SerialFrame> parseSerialFrame(std::span<const uint8_t> data);

}  // namespace radio::core::rsba1
