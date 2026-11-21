// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/satellite_tracker.h — W2.4 双 VFO 自动跟踪的「会话执行者」接线
//
// 分工（总纲 §5.5）：
//   satellite/track_policy.*  纯函数：该不该写、写多少；
//   satellite/sat_track.*     纯数学：此刻方向 / ṙ / 多普勒；
//   本类                      胶水：1s 节拍取此刻 → 过策略 → 调注入的 apply。
//
// 硬约束（A-2.6）：
//   · 发射中一律不写（策略层拒写，本类每拍都把 tx 状态喂进去）；
//   · 死区与最小间隔由 trackDecide 判定，本类不另起一套；
//   · LOS 后自动停，并且停这件事必须能被界面看见（trackingStopped）。
//
// 时钟可注入：tick() 公开、NowFn 可替换，测试不需要等真实一秒。
#pragma once

#include <libsgp4/DateTime.h>
#include <libsgp4/Tle.h>

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

#include "satellite/sat_track.h"
#include "satellite/track_policy.h"

class QTimer;

namespace radio::app {

class SatelliteTracker : public QObject {
  Q_OBJECT

 public:
  using ApplyFn = std::function<void(double)>;   // 由界面接到会话写频
  using TxFn = std::function<bool()>;            // 现刻是否在发射
  using NowFn = std::function<libsgp4::DateTime()>;

  SatelliteTracker(ApplyFn apply, TxFn tx, NowFn now = {}, QObject* parent = nullptr);
  ~SatelliteTracker() override;

  // 进入跟踪：el_floor 以下先等 AOS（不写），AOS 后每秒按多普勒改写。
  void start(const libsgp4::Tle& tle, const satellite::Station& st,
             double nominal_rx_hz, double el_floor_deg = 5.0);
  void stop(const QString& reason);
  bool active() const { return active_; }

  // 1s 定时器调它；测试直接调（确定性，不依赖事件循环）
  void tick();

  struct Snapshot {
    bool active = false;
    bool above_horizon = false;
    double el_deg = 0.0;
    double target_hz = 0.0;
    double applied_hz = 0.0;
    int writes = 0;
    QString status;
  };
  Snapshot snapshot() const { return snap_; }

 signals:
  void statusChanged(const QString& text);
  void trackingStopped(const QString& reason);

 private:
  void setStatus(const QString& text);
  void ensureTimer();

  ApplyFn apply_;
  TxFn tx_;
  NowFn now_;
  satellite::TrackConfig cfg_;
  std::optional<libsgp4::Tle> tle_;  // 未 start 前为空；用占位串构造会在 ctor 里抛 TleException
  satellite::Station st_{};
  double nominal_hz_ = 0.0;
  double el_floor_deg_ = 5.0;
  bool active_ = false;
  bool seen_above_ = false;
  int64_t last_write_ms_ = 0;
  Snapshot snap_;
  QTimer* timer_ = nullptr;
};

}  // namespace radio::app
