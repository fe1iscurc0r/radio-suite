// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/satellite_tracker.cpp
#include "satellite_tracker.h"

#include <libsgp4/SatelliteException.h>

#include <QTimer>

namespace radio::app {
namespace {

int64_t ticksToMs(int64_t ticks) { return ticks / 10000; }  // 100ns ticks

}  // namespace

SatelliteTracker::SatelliteTracker(ApplyFn apply, TxFn tx, NowFn now, QObject* parent)
    : QObject(parent),
      apply_(std::move(apply)),
      tx_(std::move(tx)),
      now_(now ? std::move(now)
               : NowFn([] { return libsgp4::DateTime::Now(true); })) {}

SatelliteTracker::~SatelliteTracker() = default;

void SatelliteTracker::ensureTimer() {
  if (timer_) return;
  timer_ = new QTimer(this);
  timer_->setInterval(1000);
  connect(timer_, &QTimer::timeout, this, [this] { tick(); });
}

void SatelliteTracker::start(const libsgp4::Tle& tle, const satellite::Station& st,
                             double nominal_rx_hz, double el_floor_deg) {
  tle_ = tle;
  st_ = st;
  nominal_hz_ = nominal_rx_hz;
  el_floor_deg_ = el_floor_deg;
  active_ = true;
  seen_above_ = false;
  last_write_ms_ = 0;
  snap_ = Snapshot{};
  snap_.active = true;
  ensureTimer();
  timer_->start();
  tick();
}

void SatelliteTracker::stop(const QString& reason) {
  if (!active_) return;
  active_ = false;
  if (timer_) timer_->stop();
  snap_.active = false;
  setStatus(QStringLiteral("跟踪已停：%1").arg(reason));
  emit trackingStopped(reason);
}

void SatelliteTracker::setStatus(const QString& text) {
  snap_.status = text;
  emit statusChanged(text);
}

void SatelliteTracker::tick() {
  if (!active_ || !tle_) return;

  const auto now = now_();
  const double tsince = satellite::tsince_min(*tle_, now);
  satellite::Look look{};
  try {
    look = satellite::look_at(*tle_, tsince, st_);
  } catch (const libsgp4::SatelliteException& e) {
    stop(QStringLiteral("轨道根数异常：%1").arg(QString::fromStdString(e.what())));
    return;
  }

  const bool above = look.el_deg >= el_floor_deg_;
  const double target = nominal_hz_ + satellite::doppler_hz(nominal_hz_, look.range_rate_kms);
  const int64_t now_ms = ticksToMs(now.Ticks());
  const bool tx_active = tx_ ? tx_() : false;

  if (above) seen_above_ = true;

  // LOS 判定要区分「过境前的等待」与「过境后的离开」：没上过天就停会把预置窗
  // 直接掐掉；上过天才停，才是真的过境结束。
  if (!above && seen_above_) {
    stop(QStringLiteral("已过 LOS（仰角 %.1f° < %.1f°）")
             .arg(look.el_deg)
             .arg(el_floor_deg_));
    return;
  }

  const auto d = satellite::trackDecide(cfg_, target, snap_.applied_hz, tx_active,
                                        now_ms, last_write_ms_);
  if (d.write && above) {
    if (apply_) apply_(d.rx_hz);
    snap_.applied_hz = d.rx_hz;
    last_write_ms_ = now_ms;
    ++snap_.writes;
  }

  snap_.active = true;
  snap_.above_horizon = above;
  snap_.el_deg = look.el_deg;
  snap_.target_hz = target;

  QString s;
  if (!above) {
    s = QStringLiteral("等待 AOS（仰角 %.1f°）· 标称 %1 Hz")
            .arg(look.el_deg)
            .arg(nominal_hz_, 0, 'f', 0);
  } else {
    s = QStringLiteral("跟踪中 · 仰角 %1° · 目标 %2 Hz · 已写 %3 Hz · 第 %4 次写%5")
            .arg(look.el_deg, 0, 'f', 1)
            .arg(target, 0, 'f', 0)
            .arg(snap_.applied_hz, 0, 'f', 0)
            .arg(snap_.writes)
            .arg(tx_active ? QStringLiteral("（发射中：策略拒写）") : QString());
    if (!d.write) s += QStringLiteral(" · 未写：%1").arg(QString::fromStdString(d.reason));
  }
  setStatus(s);
}

}  // namespace radio::app
