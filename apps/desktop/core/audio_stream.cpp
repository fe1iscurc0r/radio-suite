// SPDX-License-Identifier: MIT
// Ported from kappanhang (MIT) — 见 audio_stream.h 头部出处说明。
#include "core/audio_stream.h"

#include <algorithm>
#include <cstring>

namespace radio::core::rsba1 {

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

namespace {

// 上游 handleRead：len >= 580（part2 包最小）且前 6 字节为两种包头之一
bool isAudioPacket(std::span<const uint8_t> r) {
  if (r.size() < kAudioHeaderLen + kAudioPart2Pcm) return false;
  static const std::array<uint8_t, 6> p1 = {0x6c, 0x05, 0x00, 0x00, 0x00, 0x00};
  static const std::array<uint8_t, 6> p2 = {0x44, 0x02, 0x00, 0x00, 0x00, 0x00};
  return std::memcmp(r.data(), p1.data(), 6) == 0 ||
         std::memcmp(r.data(), p2.data(), 6) == 0;
}

// 环形前向距离：got 比 from 超前多少包（0=同一个；≥0x8000 视为在后方）
uint16_t seqDistance(uint16_t got, uint16_t from) {
  return static_cast<uint16_t>(got - from);
}

}  // namespace

void AudioRxBuffer::drainContiguous(std::vector<Chunk>& out) {
  for (;;) {
    auto it = std::find_if(pending_.begin(), pending_.end(), [this](const Pending& p) {
      return p.seq == expected_;
    });
    if (it == pending_.end()) return;
    out.push_back(Chunk{it->seq, std::move(it->pcm)});
    last_delivered_seq_ = it->seq;
    pending_.erase(it);
    ++expected_;
  }
}

AudioRxBuffer::Push AudioRxBuffer::feed(std::span<const uint8_t> udp_packet,
                                        int64_t now_ms) {
  Push out;
  if (!isAudioPacket(udp_packet)) return out;
  const uint16_t seq =
      static_cast<uint16_t>(udp_packet[6] | (udp_packet[7] << 8));
  std::vector<uint8_t> pcm(udp_packet.begin() + kAudioHeaderLen, udp_packet.end());

  if (received_) {
    const uint16_t dist = seqDistance(seq, expected_);
    if (dist >= 0x8000) {  // 迟到（含迟到的重传包）或重复
      out.dropped = true;
      ++dropped_;
      return out;
    }
    if (dist != 0) {
      // 未来包：等缺口补齐；已有同 seq 视为重复
      for (const Pending& p : pending_) {
        if (p.seq == seq) {
          out.dropped = true;
          ++dropped_;
          return out;
        }
      }
      if (pending_.size() >= kMaxPending) {
        // 序号异常（非丢包）：丢掉最远的未来包，保住内存与当前顺序
        auto far = pending_.begin();
        for (auto it = pending_.begin(); it != pending_.end(); ++it)
          if (seqDistance(it->seq, expected_) > seqDistance(far->seq, expected_))
            far = it;
        pending_.erase(far);
        ++dropped_;
      }
      pending_.push_back(Pending{seq, std::move(pcm), now_ms});
      return out;
    }
  } else {
    received_ = true;
    expected_ = seq;  // 首个包建立基线（上游 alreadyReturnedFirstSeq 之前不判序）
  }

  out.ready.push_back(Chunk{seq, std::move(pcm)});
  last_delivered_seq_ = seq;
  ++expected_;
  drainContiguous(out.ready);
  return out;
}

std::vector<AudioRxBuffer::Chunk> AudioRxBuffer::flush(int64_t now_ms) {
  std::vector<Chunk> out;
  while (!pending_.empty()) {
    // 缺口后的下一包 = 距离 expected_ 最近的缓存项
    auto best = pending_.begin();
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
      if (seqDistance(it->seq, expected_) < seqDistance(best->seq, expected_))
        best = it;
    if (now_ms - best->at_ms < kReorderWindowMs) break;  // 窗口未到，继续等
    lost_ += seqDistance(best->seq, expected_);
    expected_ = best->seq;
    out.push_back(Chunk{best->seq, std::move(best->pcm)});
    last_delivered_seq_ = best->seq;
    pending_.erase(best);
    ++expected_;
    drainContiguous(out);
  }
  return out;
}

}  // namespace radio::core::rsba1
