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

// ---- RX：100ms 重排窗（对齐 kappanhang seqBuf，见 seqbuf.go / audiostream.go）----
// 上游语义：乱序包先入队，只有"期望序号"能出队；缺口出现后锁 100ms
// （同时向电台请求重传），超时后把缺口计为丢包、放行后续。
// 差异：重传请求包格式未在真机抓包里确认，这里只做超时放行，不发重传请求。
// 缺口只在真丢包时引入 100ms 静音延迟；顺序流零等待。
class AudioRxBuffer {
 public:
  struct Chunk {
    uint16_t seq = 0;
    std::vector<uint8_t> pcm;  // ≤1364B
  };
  struct Push {
    std::vector<Chunk> ready;  // 本次可投递（按序）
    bool dropped = false;      // 迟到/重复包丢弃
  };

  // 喂一个音频 UDP 包（24B 头 + 载荷；上游 handleRead 要求总长 ≥580）
  Push feed(std::span<const uint8_t> udp_packet, int64_t now_ms);
  // 重排窗到期检查：最老缺口等待 ≥ kReorderWindowMs 后计丢包并放行后续。
  // 每次 poll 调一次即可。
  std::vector<Chunk> flush(int64_t now_ms);

  static constexpr int64_t kReorderWindowMs = 100;
  static constexpr size_t kMaxPending = 256;  // 序号异常时的内存上限

  bool receivedAnything() const { return received_; }
  uint16_t lastDeliveredSeq() const { return last_delivered_seq_; }
  size_t lostPackets() const { return lost_; }
  size_t droppedPackets() const { return dropped_; }
  size_t pendingPackets() const { return pending_.size(); }

 private:
  struct Pending {
    uint16_t seq = 0;
    std::vector<uint8_t> pcm;
    int64_t at_ms = 0;
  };
  void drainContiguous(std::vector<Chunk>& out);
  bool received_ = false;
  uint16_t expected_ = 0;  // 下一个期望 seq
  uint16_t last_delivered_seq_ = 0;
  std::vector<Pending> pending_;
  size_t lost_ = 0;
  size_t dropped_ = 0;
};

}  // namespace radio::core::rsba1
