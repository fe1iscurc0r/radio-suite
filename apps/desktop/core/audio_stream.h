// SPDX-License-Identifier: MIT
// Ported from: https://github.com/nonoo/kappanhang @ master (MIT)
//   - audiostream.go  : sendPart1/sendPart2 包格式、handleAudioPacket RX 识别
//   - seqbuf.go       : 100ms 重排窗 / 乱序丢弃 / 丢包计数
//   - audio-linux.go  : 48kHz s16le mono、20ms 帧 = 1920B、静音帧跳发
// Upstream copyright: © 2020 Norbert Varga HA2NON, Akos Marton ES1AKOS
// Local modifications: Go→C++20，QUdpSocket 无关（纯字节层，传输由调用方注入）；
//                      目标落点 libs/protocol/rsba1/（monorepo 平移时 git mv）。
//
// RS-BA1 V2 音频通路（UDP 50003）——补 rsba1-core 的音频真空：
//   RX：电台 → 客户端。识别特征 = 前 6 字节 {0x6c,0x05,0,0,0,0} 或
//       {0x44,0x02,0,0,0,0} 且总长 ≥580；seq = [6:8] LE；音频载荷自 [24:] 起。
//   TX：客户端 → 电台。20ms 帧（1920B PCM）拆两包：
//       part1 头 24B + 1364B；part2 头 24B + 556B；
//       头内 abs[16]=0x80, abs[18:20]=(seq-1) BE, abs[22:24]=payloadLen BE。
//       audioSendSeq 从 1 递增；全零帧跳发（省带宽，与上游一致）。
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <span>
#include <vector>

namespace radio::core::rsba1 {

inline constexpr uint32_t kAudioSampleRate = 48000;
inline constexpr size_t kAudioFrameBytes = 1920;  // 20ms @ 48kHz s16le mono
inline constexpr size_t kAudioPart1Pcm = 1364;
inline constexpr size_t kAudioPart2Pcm = 556;
inline constexpr size_t kAudioHeaderLen = 24;
inline constexpr uint16_t kAudioSeqWrap = 0xFFFF;  // seq 环绕

// ---- TX：20ms PCM 帧 → 两个音频 UDP 包（kappanhang sendPart1/2）----
class AudioTxPacker {
 public:
  void setSids(uint32_t local_sid, uint32_t remote_sid);
  // 输入恰好 1920B（不足补零由调用方决定；全零帧返回空 = 上游 isAllZero 跳发语义）
  std::vector<std::vector<uint8_t>> packFrame(std::span<const uint8_t> pcm20ms);

  uint16_t seq() const { return send_seq_; }

 private:
  std::vector<uint8_t> makePart(std::array<uint8_t, 2> magic, uint16_t len_be,
                                std::span<const uint8_t> pcm);
  uint32_t local_sid_ = 0, remote_sid_ = 0;
  uint16_t send_seq_ = 1;  // 上游 init：audioSendSeq = 1
};

// ---- RX：100ms 重排窗（kappanhang seqBuf 语义子集）----
class AudioRxBuffer {
 public:
  // 喂入一个识别为音频的 UDP 包（含 24B 头）；返回本次是否产出载荷
  struct Event {
    bool delivered = false;     // 顺序到达并产出
    bool dropped = false;       // 乱序迟到丢弃
    int lost = 0;               // 本次事件中判定的丢包数
    std::vector<uint8_t> pcm;   // delivered 时为载荷（≤1364B）
  };
  Event feed(std::span<const uint8_t> udp_packet);

  bool receivedAnything() const { return received_; }
  uint16_t lastSeq() const { return last_seq_; }

 private:
  bool received_ = false;
  uint16_t last_seq_ = 0;
  std::map<uint16_t, std::vector<uint8_t>> pending_;  // 提前到的包暂存
};

}  // namespace radio::core::rsba1
