// SPDX-License-Identifier: AGPL-3.0-or-later
// 音频设置对话框（离屏）：设备表注入、ID 往返、统计刷新——不需要真声卡。
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QComboBox>
#include <QLabel>

#include <functional>
#include <vector>

#include "app/audio_dialog.h"
#include "app/audio_stats.h"

namespace {

using radio::app::AudioSettingsDialog;
using radio::app::AudioStats;

QApplication& app() {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  static int argc = 1;
  static char name[] = "radio-audio-ui-tests";
  static char* argv[] = {name, nullptr};
  static QApplication instance(argc, argv);
  return instance;
}

}  // namespace

TEST_CASE("audio stats line carries every counter", "[ui][audio]") {
  AudioStats s;
  s.rx_chunks = 12;
  s.rx_pending = 2;
  s.rx_lost = 3;
  s.rx_dropped = 1;
  s.tx_frames = 40;
  s.tx_dropped = 5;
  s.sink_queued = 960;
  s.sink_underruns = 7;
  s.sink_dropped = 8;
  s.mic_overflows = 9;

  const auto text = radio::app::formatAudioStats(s);
  CHECK(text.contains("RX 12 块"));
  CHECK(text.contains("待发 2"));
  CHECK(text.contains("丢 3"));
  CHECK(text.contains("弃 1"));
  CHECK(text.contains("TX 40 帧"));
  CHECK(text.contains("积压 960"));
  CHECK(text.contains("欠载 7"));
  CHECK(text.contains("溢出 9"));
}

TEST_CASE("audio dialog lists devices and keeps ids", "[ui][audio]") {
  app();
  AudioSettingsDialog dlg;
  dlg.setDevices({{3, "Mic A"}, {7, "Mic B"}}, {{2, "Spk X"}}, 7, -1);

  auto* in = dlg.findChild<QComboBox*>("audio_in_combo");
  auto* out = dlg.findChild<QComboBox*>("audio_out_combo");
  REQUIRE(in != nullptr);
  REQUIRE(out != nullptr);
  // 首项恒为"系统默认"（WASAPI ID 非连续，用 data 而不是下标定位）
  CHECK(in->count() == 3);
  CHECK(in->itemData(0).toInt() == -1);
  CHECK(in->currentData().toInt() == 7);
  CHECK(out->count() == 2);
  CHECK(dlg.chosenInputId() == 7);
  CHECK(dlg.chosenOutputId() == -1);

  in->setCurrentIndex(1);
  CHECK(dlg.chosenInputId() == 3);

  // 设备被拔掉：未知 id 回落"系统默认"，不能把非法 id 交给设备层
  dlg.setDevices({{3, "Mic A"}}, {}, 99, 99);
  CHECK(dlg.chosenInputId() == -1);
  CHECK(dlg.chosenOutputId() == -1);
}

TEST_CASE("audio dialog refreshes stats from provider", "[ui][audio]") {
  app();
  AudioSettingsDialog dlg;
  auto* label = dlg.findChild<QLabel*>("audio_stats_label");
  REQUIRE(label != nullptr);

  std::function<AudioStats()> none;
  dlg.setStatsProvider(none);
  CHECK(label->text().contains("未开启"));

  int calls = 0;
  AudioStats s;
  s.rx_lost = 4;
  dlg.setStatsProvider([&] {
    ++calls;
    return s;
  });
  CHECK(calls == 1);  // 立即出一次数，不等 500ms 定时器
  CHECK(label->text().contains("丢 4"));

  s.rx_chunks = 21;
  dlg.refreshStats();
  CHECK(label->text().contains("RX 21 块"));

  dlg.setStatsProvider(none);
  CHECK(label->text().contains("未开启"));
  CHECK(calls == 2);
}
