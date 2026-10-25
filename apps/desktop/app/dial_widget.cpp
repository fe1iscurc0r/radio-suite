// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/dial_widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

DialWidget::DialWidget(QWidget* parent) : QFrame(parent) {
  setMinimumSize(120, 120);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setCursor(Qt::SizeAllCursor);
}

void DialWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QPointF c(width() / 2.0, height() / 2.0);
  const double r = std::min(width(), height()) * 0.46;

  // 外圈
  QRadialGradient grad(c, r);
  grad.setColorAt(0.0, QColor(48, 54, 62));
  grad.setColorAt(0.8, QColor(28, 32, 38));
  grad.setColorAt(1.0, QColor(16, 19, 24));
  p.setBrush(grad);
  p.setPen(QPen(QColor(80, 90, 100), 2));
  p.drawEllipse(c, r, r);

  // 周边刻度点（模拟原厂刻度环）
  p.setPen(Qt::NoPen);
  for (int i = 0; i < 48; ++i) {
    const float a = float(i) / 48.0f * 360.0f;
    const bool major = i % 6 == 0;
    const double rr = r * (major ? 0.86 : 0.90);
    p.setBrush(QColor(major ? 200 : 120, major ? 208 : 128, major ? 216 : 136));
    p.drawEllipse(QPointF(c.x() + std::cos(a * M_PI / 180) * rr,
                          c.y() + std::sin(a * M_PI / 180) * rr),
                  major ? 2.2 : 1.4, major ? 2.2 : 1.4);
  }

  // 指示标线（随 angle_ 旋转）
  p.save();
  p.translate(c);
  p.rotate(angle_);
  p.setPen(QPen(QColor(255, 210, 90), 3));
  p.drawLine(QPointF(0, -r * 0.42), QPointF(0, -r * 0.95));
  p.restore();

  // 中心
  p.setPen(QPen(QColor(90, 100, 110), 1));
  p.setBrush(QColor(20, 23, 28));
  p.drawEllipse(c, r * 0.36, r * 0.36);
  p.setPen(QColor(160, 170, 180));
  p.setFont(QFont("Consolas", 8));
  p.drawText(QRectF(c.x() - r, c.y() + r * 0.5, 2 * r, 18), Qt::AlignHCenter,
             "M-TUNE");
}

void DialWidget::wheelEvent(QWheelEvent* e) {
  const int ticks = e->angleDelta().y() / 120;  // 标准滚轮一格
  if (ticks == 0) return;
  angle_ = std::fmod(angle_ + ticks * 15.0f + 360.0f, 360.0f);
  emit steps(ticks);
  update();
  e->accept();
}

void DialWidget::mousePressEvent(QMouseEvent* e) {
  dragging_ = true;
  drag_last_angle_ = std::atan2(e->position().y() - height() / 2.0,
                                e->position().x() - width() / 2.0) *
                     180.0 / M_PI;
  e->accept();
}

void DialWidget::mouseMoveEvent(QMouseEvent* e) {
  if (!dragging_) return;
  const float a = std::atan2(e->position().y() - height() / 2.0,
                             e->position().x() - width() / 2.0) *
                  180.0f / M_PI;
  float d = a - drag_last_angle_;
  if (d > 180.0f) d -= 360.0f;
  if (d < -180.0f) d += 360.0f;
  drag_last_angle_ = a;
  angle_ = std::fmod(angle_ + d + 360.0f, 360.0f);
  // 每 18° 一格（转一圈 20 步），手感接近原厂慢轮
  static float acc = 0.0f;
  acc += d;
  const int steps_out = static_cast<int>(acc / 18.0f);
  if (steps_out != 0) {
    acc -= steps_out * 18.0f;
    emit steps(steps_out);
  }
  update();
  e->accept();
}
