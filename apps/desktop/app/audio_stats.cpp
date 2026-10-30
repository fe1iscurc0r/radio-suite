// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/audio_stats.h"

namespace radio::app {

QString formatAudioStats(const AudioStats& s) {
  return QString(
             "RX %1 块 · 待发 %2 · 丢 %3 · 弃 %4\n"
             "TX %5 帧 · 弃 %6\n"
             "扬声器 积压 %7 · 欠载 %8 · 丢 %9\n"
             "麦克风 溢出 %10")
      .arg(s.rx_chunks)
      .arg(s.rx_pending)
      .arg(s.rx_lost)
      .arg(s.rx_dropped)
      .arg(s.tx_frames)
      .arg(s.tx_dropped)
      .arg(s.sink_queued)
      .arg(s.sink_underruns)
      .arg(s.sink_dropped)
      .arg(s.mic_overflows);
}

}  // namespace radio::app
