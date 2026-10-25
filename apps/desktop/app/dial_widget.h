// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/dial_widget.h — 主调谐旋钮（原厂 M-Tune 风格件）
// 拖拽/滚轮 → steps 信号（步数按当前步进档换算频率），工单 M3"调谐轮"提前
#pragma once

#include <QFrame>

class DialWidget : public QFrame {
  Q_OBJECT
 public:
  explicit DialWidget(QWidget* parent = nullptr);

 signals:
  void steps(int count);  // 正=升频，负=降频（拖拽按角度累计、滚轮按格）

 protected:
  void paintEvent(QPaintEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;

 private:
  float angle_ = 0.0f;      // 指示角（度）
  float drag_last_angle_ = 0.0f;
  bool dragging_ = false;
};
