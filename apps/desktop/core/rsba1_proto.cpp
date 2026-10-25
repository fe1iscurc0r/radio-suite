// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/rsba1_proto.h"

#include <cstring>

namespace radio::core::rsba1 {
namespace {

// ICOM 95 项置换表（索引 p-32，p∈[32,126]）。由参考实现运行时导出
// （tests/fixtures/gen_from_reference.py 有互锁校验），杜绝手抄错位。
constexpr uint8_t kPasscodeTable[95] = {
    0x47, 0x5D, 0x4C, 0x42, 0x66, 0x20, 0x23, 0x46, 0x4E, 0x57, 0x45, 0x3D,
    0x67, 0x76, 0x60, 0x41, 0x62, 0x39, 0x59, 0x2D, 0x68, 0x7E, 0x7C, 0x65,
    0x7D, 0x49, 0x29, 0x72, 0x73, 0x78, 0x21, 0x6E, 0x5A, 0x5E, 0x4A, 0x3E,
    0x71, 0x2C, 0x2A, 0x54, 0x3C, 0x3A, 0x63, 0x4F, 0x43, 0x75, 0x27, 0x79,
    0x5B, 0x35, 0x70, 0x48, 0x6B, 0x56, 0x6F, 0x34, 0x32, 0x6C, 0x30, 0x61,
    0x6D, 0x7B, 0x2F, 0x4B, 0x64, 0x38, 0x2B, 0x2E, 0x50, 0x40, 0x3F, 0x55,
    0x33, 0x37, 0x25, 0x77, 0x24, 0x26, 0x74, 0x6A, 0x28, 0x53, 0x4D, 0x69,
    0x22, 0x5C, 0x44, 0x31, 0x36, 0x58, 0x3B, 0x7A, 0x51, 0x5F, 0x52,
};

void putLe16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
  b[off] = static_cast<uint8_t>(v & 0xFF);
  b[off + 1] = static_cast<uint8_t>(v >> 8);
}
void putLe32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[off + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}
void putBe16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
  b[off] = static_cast<uint8_t>(v >> 8);
  b[off + 1] = static_cast<uint8_t>(v & 0xFF);
}
void putBe32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b[off + static_cast<size_t>(3 - i)] = static_cast<uint8_t>(v >> (8 * i));
}
uint16_t getLe16(std::span<const uint8_t> b, size_t off) {
  return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}
uint16_t getBe16(std::span<const uint8_t> b, size_t off) {
  return static_cast<uint16_t>((b[off] << 8) | b[off + 1]);
}
uint32_t getBe32(std::span<const uint8_t> b, size_t off) {
  return (static_cast<uint32_t>(b[off]) << 24) | (static_cast<uint32_t>(b[off + 1]) << 16) |
         (static_cast<uint32_t>(b[off + 2]) << 8) | b[off + 3];
}

// 内层业务头（严格 0x10B，BE version/type；inner_seq 低字节在 +0x07、高字节在 +0x08）
std::vector<uint8_t> bizHeader(uint16_t version, uint16_t type_cmd, uint8_t inner_seq,
                               std::span<const uint8_t> id4) {
  std::vector<uint8_t> h(16, 0);
  putBe16(h, 2, version);
  putBe16(h, 4, type_cmd);
  h[7] = inner_seq;                       // +0x06=0x00, +0x07=seq 低 8 位
  for (size_t i = 0; i < 4 && i < id4.size(); ++i) h[0x0A + i] = id4[i];
  return h;
}

}  // namespace

uint32_t makeLocalSid(uint32_t ip_be, uint16_t port) {
  return ((ip_be & 0xFFFF) << 16) | (port & 0xFFFF);
}

std::array<uint8_t, 16> passcode(const std::string& text) {
  std::array<uint8_t, 16> out{};
  const size_t n = text.size() < 16 ? text.size() : 16;
  for (size_t i = 0; i < n; ++i) {
    int p = static_cast<uint8_t>(text[i]) + static_cast<int>(i);
    if (p > 126) p = 32 + p % 127;
    out[i] = kPasscodeTable[static_cast<size_t>(p - 32)];
  }
  return out;
}

std::vector<uint8_t> cmdTransportHeader(uint32_t total_len, uint16_t type,
                                        uint16_t seq, uint32_t local_sid,
                                        uint32_t remote_sid) {
  std::vector<uint8_t> h(16, 0);
  putLe32(h, 0, total_len);
  putLe16(h, 4, type);
  putLe16(h, 6, seq);
  putBe32(h, 8, local_sid);   // Command 信道 SID = BE（历史定案）
  putBe32(h, 12, remote_sid);
  return h;
}

std::optional<WireHeader> parseSerWire(std::span<const uint8_t> data) {
  if (data.size() < 16) return std::nullopt;
  WireHeader w;
  w.type = getLe16(data, 4);
  w.seq = getLe16(data, 6);
  w.f8 = static_cast<uint32_t>(data[8]) | (static_cast<uint32_t>(data[9]) << 8) |
         (static_cast<uint32_t>(data[10]) << 16) | (static_cast<uint32_t>(data[11]) << 24);
  w.fc = static_cast<uint32_t>(data[12]) | (static_cast<uint32_t>(data[13]) << 8) |
         (static_cast<uint32_t>(data[14]) << 16) | (static_cast<uint32_t>(data[15]) << 24);
  return w;
}

std::vector<uint8_t> serWireHeader(uint16_t type, uint16_t seq, uint32_t f8,
                                   uint32_t fc, size_t payload_len) {
  std::vector<uint8_t> h(16, 0);
  putLe32(h, 0, static_cast<uint32_t>(16 + payload_len));
  putLe16(h, 4, type);
  putLe16(h, 6, seq);
  putLe32(h, 8, f8);         // Serial 信道全 LE（2026-08-12 定案）
  putLe32(h, 12, fc);
  return h;
}

std::vector<uint8_t> buildPkt3(uint32_t local_sid) {
  return cmdTransportHeader(0x10, 0x03, 0, local_sid, 0);
}
std::vector<uint8_t> buildPkt6(uint32_t local_sid, uint32_t remote_sid) {
  return cmdTransportHeader(0x10, 0x06, 1, local_sid, remote_sid);
}
std::vector<uint8_t> buildDisconnect(uint32_t local_sid, uint32_t remote_sid) {
  return cmdTransportHeader(0x10, 0x05, 0, local_sid, remote_sid);
}
std::vector<uint8_t> buildIdle0(uint32_t local_sid, uint32_t remote_sid,
                                uint16_t seq) {
  return cmdTransportHeader(0x10, 0x00, seq, local_sid, remote_sid);
}

std::vector<uint8_t> buildPkt7(uint32_t local_sid, uint32_t remote_sid,
                               uint16_t seq,
                               std::optional<std::array<uint8_t, 4>> reply_id,
                               uint16_t inner_seq) {
  auto pkt = cmdTransportHeader(0x15, 0x07, seq, local_sid, remote_sid);
  pkt.resize(21);
  if (reply_id) {
    pkt[16] = 0x01;
    std::memcpy(pkt.data() + 17, reply_id->data(), 4);
  } else {
    pkt[16] = 0x00;
    pkt[17] = 0;
    pkt[18] = static_cast<uint8_t>(inner_seq & 0xFF);
    pkt[19] = static_cast<uint8_t>(inner_seq >> 8);
    pkt[20] = 0x06;
  }
  return pkt;
}

bool isPkt7(std::span<const uint8_t> d) {
  return d.size() == 21 && d[1] == 0x00 && d[2] == 0x00 && d[3] == 0x00 &&
         d[4] == 0x07 && d[5] == 0x00;
}
bool isIdle0(std::span<const uint8_t> d) {
  return d.size() == 16 && d[0] == 0x10 && d[1] == 0x00 && d[2] == 0x00 &&
         d[3] == 0x00 && d[4] == 0x00 && d[5] == 0x00;
}
bool isA8Packet(std::span<const uint8_t> d) {
  return d.size() == 0xA8 && d[0] == 0xA8 && d[1] == 0x00 && d[2] == 0x00 &&
         d[3] == 0x00 && d[4] == 0x00 && d[5] == 0x00;
}
std::optional<std::array<uint8_t, 16>> extractA8ReplyId(std::span<const uint8_t> d) {
  if (!isA8Packet(d)) return std::nullopt;
  std::array<uint8_t, 16> id{};
  std::memcpy(id.data(), d.data() + 66, 16);
  return id;
}

std::vector<uint8_t> buildLoginRequest(const std::string& username,
                                       const std::string& password,
                                       uint32_t local_sid, uint32_t remote_sid,
                                       uint16_t outer_seq, uint8_t inner_seq,
                                       std::array<uint8_t, 2> auth_start_id) {
  std::vector<uint8_t> pkt(0x80, 0);
  auto hdr = cmdTransportHeader(0x80, 0x00, outer_seq, local_sid, remote_sid);
  std::memcpy(pkt.data(), hdr.data(), 16);
  auto biz = bizHeader(0x0070, 0x0100, inner_seq,
                       {auth_start_id.data(), 2});
  std::memcpy(pkt.data() + 0x10, biz.data(), 16);
  auto user = passcode(username);
  auto pass = passcode(password);
  std::memcpy(pkt.data() + 0x40, user.data(), 16);  // buf+0x30 UserName
  std::memcpy(pkt.data() + 0x50, pass.data(), 16);  // buf+0x40 Password
  const char memo[] = "icom-pc\0";                  // buf+0x50 Memo（不参与认证）
  std::memcpy(pkt.data() + 0x60, memo, 8);
  return pkt;
}

std::vector<uint8_t> buildAuthRequest(uint8_t magic, uint32_t local_sid,
                                      uint32_t remote_sid, uint16_t outer_seq,
                                      uint8_t inner_seq,
                                      std::span<const uint8_t> auth_id6) {
  std::vector<uint8_t> pkt(0x40, 0);
  auto hdr = cmdTransportHeader(0x40, 0x00, outer_seq, local_sid, remote_sid);
  std::memcpy(pkt.data(), hdr.data(), 16);
  auto biz = bizHeader(0x0030, static_cast<uint16_t>(0x0100 | magic), inner_seq,
                       auth_id6.first(4));
  std::memcpy(pkt.data() + 0x10, biz.data(), 16);
  pkt[0x1E] = auth_id6[4];
  pkt[0x1F] = auth_id6[5];
  return pkt;
}

std::vector<uint8_t> buildConnectTransRequest(const ConnectTransParams& p) {
  std::vector<uint8_t> pkt(0x90, 0);
  auto hdr = cmdTransportHeader(0x90, 0x00, p.outer_seq, p.local_sid, p.remote_sid);
  std::memcpy(pkt.data(), hdr.data(), 16);
  auto biz = bizHeader(0x0080, 0x0103, p.inner_seq, p.auth_id6.first(4));
  std::memcpy(pkt.data() + 0x10, biz.data(), 16);
  pkt[0x1E] = p.auth_id6[4];
  pkt[0x1F] = p.auth_id6[5];
  std::memcpy(pkt.data() + 0x20, p.a8_reply_id16.data(), 16);
  const char model[] = "IC-705\0\0";
  std::memcpy(pkt.data() + 0x40, model, 8);
  auto user = passcode(p.username);
  std::memcpy(pkt.data() + 0x60, user.data(), 16);
  pkt[0x70] = 0x01; pkt[0x71] = 0x01; pkt[0x72] = 0x04; pkt[0x73] = 0x04;
  putBe16(pkt, 0x76, p.sample_rate);
  putBe16(pkt, 0x7A, p.sample_rate);
  putBe16(pkt, 0x7E, p.serial_port);
  putBe16(pkt, 0x82, p.audio_port);
  putBe16(pkt, 0x86, p.tx_seq_buf_ms);
  pkt[0x88] = 0x01;
  return pkt;
}

std::optional<LoginResult> parseLoginResponse(std::span<const uint8_t> d) {
  if (d.size() != 0x60) return std::nullopt;
  if (d[0] != 0x60 || d[1] != 0 || d[2] != 0 || d[3] != 0 || d[4] != 0)
    return std::nullopt;
  LoginResult r;
  std::memcpy(r.auth_id.data(), d.data() + 26, 6);
  r.result_code = static_cast<int32_t>(getBe32(d, 48));
  r.ok = r.result_code == 0;
  return r;
}

std::optional<ConnectTransResult> parseConnectTransResponse(std::span<const uint8_t> d) {
  if (d.size() != 0x90) return std::nullopt;
  if (d[0] != 0x90 || d[1] != 0 || d[2] != 0 || d[3] != 0 || d[4] != 0)
    return std::nullopt;
  ConnectTransResult r;
  r.ok = d[96] == 1;
  r.new_remote_sid = getBe32(d, 8);
  r.new_local_sid = getBe32(d, 12);
  std::memcpy(r.new_auth_id.data(), d.data() + 26, 6);
  size_t end = 64;
  while (end < d.size() && d[end] != 0) ++end;
  r.dev_name.assign(reinterpret_cast<const char*>(d.data() + 64),
                    end - 64);
  return r;
}

std::optional<uint8_t> parseAuthReplyMagic(std::span<const uint8_t> d) {
  if (d.size() == 0x40 && d[0] == 0x40 && d[1] == 0 && d[2] == 0 && d[3] == 0 &&
      d[4] == 0 && d[5] == 0)
    return d[21];
  return std::nullopt;
}

std::vector<uint8_t> buildSerialFrame(std::span<const uint8_t> payload,
                                      uint16_t sseq, bool bulk) {
  std::vector<uint8_t> f(5 + payload.size());
  f[0] = static_cast<uint8_t>(0xC0 | (bulk ? 0x01 : 0x00));
  putLe16(f, 1, static_cast<uint16_t>(payload.size()));
  f[3] = static_cast<uint8_t>(sseq >> 8);   // sseq BE
  f[4] = static_cast<uint8_t>(sseq & 0xFF);
  std::memcpy(f.data() + 5, payload.data(), payload.size());
  return f;
}

std::optional<SerialFrame> parseSerialFrame(std::span<const uint8_t> d) {
  if (d.size() < 5) return std::nullopt;
  SerialFrame f;
  f.bulk = (d[0] & 0x01) != 0;
  const uint16_t frame_len = getLe16(d, 1);
  f.sseq = getBe16(d, 3);
  if (static_cast<size_t>(frame_len) + 5 != d.size()) return std::nullopt;
  f.payload.assign(d.begin() + 5, d.end());
  return f;
}

}  // namespace radio::core::rsba1
