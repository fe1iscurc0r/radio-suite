// SPDX-License-Identifier: MIT
// Ported from kappanhang (MIT) — 见 audio_stream.h 头部出处说明。
#include "core/audio_stream.h"

#include <cstring>

namespace radio::core::rsba1 {
namespace {

bool isAudioPacketPrefix(std::span<const uint8_t> r) {
  if (r.size() < 24) return false;
  static const std::array<uint8_t, 6> p1 = {0x6c, 0x05, 0x00, 0x00, 0x00, 0x00};
  static const std::array<uint8_t, 6> p2 = {0x44, 0x02, 0x00, 0x00, 0x00, 0x00};
  return (std::memcmp(r.data(), p1.data(), 6) == 0 ||
          std::memcmp(r.data(), p2.data(), 6) == 0);
}

// 顺序距离（环形）：got 相对 last 的 +方向距离
int seqForwardDistance(uint16_t got, uint16_t last) {
  const int d = static_cast<int>(got) - static_cast<int>(last);
  return d >= 0 ? d : 65536 - last + got;
}

}  // namespace

// ---- AudioTxPacker ----

void AudioTxPacker::setSids(uint32_t local_sid, uint32_t remote_sid) {
  local_sid_ = local_sid;
  remote_sid_ = remote_sid;
}

std::vector<uint8_t> AudioTxPacker::makePart(
    std::array<uint8_t, 2> magic, uint16_t len_be, std::span<const uint8_t> pcm) {
  std::vector<uint8_t> pkt;
  pkt.reserve(kAudioHeaderLen + pcm.size());
  // 24B 头（kappanhang sendPart1/2 逐字节）：
  //   [0:2]=magic [2:8]=0 [8:12]=localSID BE [12:16]=remoteSID BE
  //   [16]=0x80 [17]=0 [18:20]=(seq-1) BE [20:22]=0 [22:24]=payloadLen BE
  pkt.insert(pkt.end(), {magic[0], magic[1], 0, 0, 0, 0, 0, 0});
  for (int i = 3; i >= 0; --i)
    pkt.push_back(static_cast<uint8_t>(local_sid_ >> (8 * i)));
  for (int i = 3; i >= 0; --i)
    pkt.push_back(static_cast<uint8_t>(remote_sid_ >> (8 * i)));
  pkt.push_back(0x80);
  pkt.push_back(0x00);
  pkt.push_back(static_cast<uint8_t>((send_seq_ - 1) >> 8));
  pkt.push_back(static_cast<uint8_t>(send_seq_ - 1));
  pkt.push_back(0x00);
  pkt.push_back(0x00);
  pkt.push_back(static_cast<uint8_t>(len_be >> 8));
  pkt.push_back(static_cast<uint8_t>(len_be & 0xFF));
  pkt.insert(pkt.end(), pcm.begin(), pcm.end());
  return pkt;
}

std::vector<std::vector<uint8_t>> AudioTxPacker::packFrame(
    std::span<const uint8_t> pcm20ms) {
  std::vector<std::vector<uint8_t>> out;
  // 上游 audio-linux.go：全零帧不发送
  bool all_zero = true;
  for (uint8_t b : pcm20ms)
    if (b != 0) { all_zero = false; break; }
  if (all_zero || pcm20ms.size() != kAudioFrameBytes) return out;

  out.push_back(makePart({0x6c, 0x05}, static_cast<uint16_t>(kAudioPart1Pcm),
                         pcm20ms.first(kAudioPart1Pcm)));
  send_seq_ = static_cast<uint16_t>(send_seq_ + 1);  // sendPart1 后 ++
  out.push_back(makePart({0x44, 0x02}, static_cast<uint16_t>(kAudioPart2Pcm),
                         pcm20ms.last(kAudioPart2Pcm)));
  send_seq_ = static_cast<uint16_t>(send_seq_ + 1);  // sendPart2 后 ++
  return out;
}

// ---- AudioRxBuffer ----

AudioRxBuffer::Event AudioRxBuffer::feed(std::span<const uint8_t> udp_packet) {
  Event ev;
  if (!isAudioPacketPrefix(udp_packet) || udp_packet.size() < 24) return ev;
  const uint16_t got =
      static_cast<uint16_t>(udp_packet[6] | (udp_packet[7] << 8));
  std::vector<uint8_t> payload(udp_packet.begin() + 24, udp_packet.end());

  if (!received_) {
    received_ = true;
    last_seq_ = got;
    ev.delivered = true;
    ev.pcm = std::move(payload);
    return ev;
  }

  // 环形"倒退超过半程"= 迟到的重传包：丢弃（上游 out-of-order 分支）
  const int fwd = seqForwardDistance(got, last_seq_);
  if (fwd == 0 || fwd > 65536 / 2) {
    ev.dropped = true;
    return ev;
  }

  const uint16_t expected = static_cast<uint16_t>(last_seq_ + 1);
  if (got != expected) {
    // 缺口：无 PCM 可补，报丢包数并放弃暂存（真实接收器在此请求重传，
    // 传输层注入后接入；纯字节层先保语义）
    ev.lost = seqForwardDistance(got, expected);
    pending_.clear();
  } else {
    // 顺序到达：若此前有提前包，这里只推进序号语义（简化 flush）
    auto it = pending_.find(expected);
    if (it != pending_.end()) pending_.erase(it);
  }

  last_seq_ = got;
  ev.delivered = true;
  ev.pcm = std::move(payload);
  return ev;
}

}  // namespace radio::core::rsba1
