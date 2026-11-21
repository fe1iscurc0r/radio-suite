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
#include <optional>

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
  // 跟踪请求（W2.4）：面板只出「开始/停止」，写频由 MainWindow 的 SatelliteTracker 执行。
  signals:
    void requestFill(double rx_hz, double tx_hz, const QString& note);
    void trackToggled(bool on);

 public slots:
  void setTrackingActive(bool on);
  void setTrackerStatus(const QString& text);

 public:
  bool hasTle() const { return tle_valid_ && tle_.has_value() && !ephemeris_stale_; }
  const libsgp4::Tle& currentTle() const { return *tle_; }
  radio::satellite::Station station() const;
  double nominalDownlinkHz() const;
  QString currentName() const;

 private:
  void refreshNow();     // 1s：此刻 az/el/斜距/ṙ + 多普勒
  void refreshPasses();  // 60s：未来 24h 过境窗（粗+细收敛）
  void reloadCatalog();  // 打开时:扫缓存目录重载星历
  void setActionsEnabled(bool on);  // 填频/跟踪统一开关（含星历过期闸）
  double nominalUplinkHz() const;

  std::unique_ptr<radio::satellite::TleStore> store_;
  std::vector<radio::satellite::TleEntry> entries_;
  int selected_ = -1;
  // 占位 Tle 会抛 TleException（"1 1" 长度非法）——真机上启动即崩的教训
  // （evidence/2026-11-22-k40/）。未选中卫星时就是没有 Tle 对象，用 optional。
  std::optional<libsgp4::Tle> tle_;
  bool tle_valid_ = false;
  bool ephemeris_stale_ = false;

  QComboBox* sat_combo_ = nullptr;
  QLabel* ephemeris_label_ = nullptr;
  QLabel* now_label_ = nullptr;
  QLabel* pass_label_ = nullptr;
  QLabel* freq_label_ = nullptr;
  QLabel* tx_label_ = nullptr;
  QPushButton* fill_btn_ = nullptr;
  QPushButton* track_btn_ = nullptr;
  QString track_status_;
  QTimer* now_timer_ = nullptr;
  QTimer* pass_timer_ = nullptr;
};

}  // namespace radio::app