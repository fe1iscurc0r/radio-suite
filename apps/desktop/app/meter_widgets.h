// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/meter_widgets.h — 仪表三件套（主信息区，显眼件）
//   SMeterWidget：半圆指针 S 表（S1..S9+60dB，指针平滑）
//   PoSwrWidget：Po（0-10W 绿）+ SWR（1-6，>3 红区）双横条
#pragma once

#include <QFrame>
#include <QString>

class SMeterWidget : public QFrame {
  Q_OBJECT
 public:
  explicit SMeterWidget(QWidget* parent = nullptr);
  // dbm ∈ [-127, -13]（120=S9=-73dBm，241=S9+60）；每帧由状态刷新驱动
  void setDbm(float dbm);

 protected:
  void paintEvent(QPaintEvent*) override;
  void timerEvent(QTimerEvent*) override;

 private:
  float dbm_ = -127.0f;
  float needle_ = -127.0f;  // 指针显示值（向目标缓动，出"表感"）
};

class PoSwrWidget : public QFrame {
  Q_OBJECT
 public:
  explicit PoSwrWidget(QWidget* parent = nullptr);
  void setPoSwr(float po_w, float swr);

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  float po_w_ = 0.0f, swr_ = 0.0f;
};
