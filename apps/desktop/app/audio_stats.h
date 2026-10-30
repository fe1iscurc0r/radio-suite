// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/audio_stats.h — 50003 音频质量快照 + 单行文本
//
// 只依赖 QtCore：设置对话框与离屏测试共用，不把 RtAudio/设备层拉进 UI 测试目标。
// 数值来源：链路（IAudioLink::rxStats）、PTT 门禁（AudioBridge）、
// 扬声器队列（AudioSink）、采集溢出（AudioIn）。
#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>

namespace radio::app {

struct AudioStats {
  // 下行（电台 → 扬声器）
  uint64_t rx_chunks = 0;     // 已按序上抛的块数
  uint32_t rx_pending = 0;    // 重排窗内待发
  uint32_t rx_lost = 0;       // 缺口超时判丢
  uint32_t rx_dropped = 0;    // 迟到/重复/超窗丢弃
  // 上行（麦克风 → PTT 门禁）
  uint64_t tx_frames = 0;     // 已上行 20ms 帧
  size_t tx_dropped = 0;      // 门禁/半双工丢弃
  // 设备侧
  size_t sink_queued = 0;     // 扬声器积压样本
  size_t sink_dropped = 0;    // 队列满/切相位丢弃样本
  size_t sink_underruns = 0;  // 输出欠载（爆音）
  size_t mic_overflows = 0;   // 采集溢出（系统丢样）
};

// 单行诊断：给对话框标签直接显示（数字用千分位以外的原样，便于对账日志）
QString formatAudioStats(const AudioStats& s);

}  // namespace radio::app
