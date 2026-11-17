// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/satellite_panel.h — 卫星页（桌面 big-button 独立窗 + 手机第 6 页签）
//
// 一屏只回答四件事（总纲 §5.4）：①选星 ②此刻 az/el/斜距/ṙ ③下一次过境
// ④上下行该打多少。纵向预算（376dp 可用高）：星历行 24 + 选星行 44 + 指标行 44
// + 过境两行 48 + 填频行 44 ≈ 204dp，余量给页签栏与顶栏——不滚动、不裁字。
//
// 口径：填频只填不发射（发射仍走 SafetyGate 与两击门禁，A-2.8 不受影响）。
// TX 侧本版只显示 linear_uplink_hz 的结果，写电台（VFO B）归 W2.4 跟踪层。
#pragma once

#include <QWidget>

#include <memory>

#include "satellite/sat_track.h"
#include "satellite/tle_source.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

namespace radio::app {

class SatellitePanel : public QWidget {
  Q_OBJECT

 public:
  explicit SatellitePanel(QWidget* parent = nullptr);

  // 填频请求（只填不发射）。rx_hz = 下行标称 + 现刻多普勒；tx_hz = linear_uplink 结果。
  signals:
    void requestFill(double rx_hz, double tx_hz, const QString& note);

 private:
  void refreshNow();     // 1s：此刻 az/el/斜距/ṙ + 多普勒
  void refreshPasses();  // 60s：未来 24h 过境窗（粗+细收敛）
  void reloadCatalog();  // 打开时:扫缓存目录重载星历
  radio::satellite::Station station() const;
  double nominalDownlinkHz() const;
  double nominalUplinkHz() const;
  QString currentName() const;

  std::unique_ptr<radio::satellite::TleStore> store_;
  std::vector<radio::satellite::TleEntry> entries_;
  int selected_ = -1;
  libsgp4::Tle tle_ = libsgp4::Tle("1 1", "2 1");  // 占位，选中后替换
  bool tle_valid_ = false;
  bool ephemeris_stale_ = false;

  QComboBox* sat_combo_ = nullptr;
  QLabel* ephemeris_label_ = nullptr;
  QLabel* now_label_ = nullptr;
  QLabel* pass_label_ = nullptr;
  QLabel* freq_label_ = nullptr;
  QLabel* tx_label_ = nullptr;
  QPushButton* fill_btn_ = nullptr;
  QTimer* now_timer_ = nullptr;
  QTimer* pass_timer_ = nullptr;
};

}  // namespace radio::app