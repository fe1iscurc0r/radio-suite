// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/meter_widgets.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {
// S 表刻度映射：dbm → 弧上 [0,1]
float dbmToNorm(float dbm) {
  const float lo = -127.0f, hi = -13.0f;  // S0 .. S9+60dB
  float v = (dbm - lo) / (hi - lo);
  return std::clamp(v, 0.0f, 1.0f);
}
}  // namespace

SMeterWidget::SMeterWidget(QWidget* parent) : QFrame(parent) {
  // 横向限宽：与紧凑旋钮台同排，把纵向空间让给频谱
  setMinimumSize(240, 108);
  setMaximumSize(320, 124);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  startTimer(33);  // 指针缓动动画
}

void SMeterWidget::setDbm(float dbm) { dbm_ = dbm; }

void SMeterWidget::timerEvent(QTimerEvent*) {
  // 机械表针缓动（快慢两个时间常数：升快降慢）
  const float target = dbmToNorm(dbm_);
  const float cur = dbmToNorm(needle_);
  const float k = target > cur ? 0.35f : 0.08f;
  const float next = cur + (target - cur) * k;
  if (std::abs(next - cur) < 0.0005f) return;
  needle_ = -127.0f + next * 114.0f;
  update();
}

void SMeterWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const int w = width(), h = height();
  const QPointF c(w * 0.5, h * 0.92);
  const float r = std::min(w * 0.46f, h * 0.82f);

  // 表盘底
  p.setBrush(QColor(12, 14, 18));
  p.setPen(QPen(QColor(70, 78, 90), 2));
  QPainterPath face;
  face.moveTo(c.x() - r, c.y());
  face.arcTo(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), 180, 180);
  face.closeSubpath();
  p.drawPath(face);

  // 刻度：S1..S9 每 S 一大格，S9 后 +20/+40/+60
  p.setPen(QPen(QColor(210, 216, 224), 1));
  for (int s = 0; s <= 15; ++s) {
    // S0..S9 → norm 0..0.75；+20..+60 → 0.75..1
    float norm;
    QString label;
    if (s <= 9) {
      norm = s / 9.0f * 0.75f;
      if (s % 3 == 0 || s == 9) label = QString("S%1").arg(s);
    } else {
      norm = 0.75f + (s - 9) / 6.0f * 0.25f;
      if ((s - 9) % 2 == 0) label = QString("+%1").arg((s - 9) * 10);
    }
    const float ang = 180.0f - norm * 180.0f;  // 左=0
    const QPointF o(c.x() + std::cos(ang * M_PI / 180) * r,
                    c.y() - std::sin(ang * M_PI / 180) * r);
    const QPointF i2(c.x() + std::cos(ang * M_PI / 180) * r * 0.86f,
                     c.y() - std::sin(ang * M_PI / 180) * r * 0.86f);
    // S9 之后红区
    p.setPen(QPen(s >= 9 ? QColor(255, 90, 80) : QColor(210, 216, 224),
                  s % 3 == 0 ? 2 : 1));
    p.drawLine(i2, o);
    if (!label.isEmpty()) {
      const QPointF t(c.x() + std::cos(ang * M_PI / 180) * r * 0.70f,
                      c.y() - std::sin(ang * M_PI / 180) * r * 0.70f);
      p.setPen(QColor(190, 198, 208));
      p.setFont(QFont("Consolas", 8));
      p.drawText(QRectF(t.x() - 20, t.y() - 8, 40, 14),
                 Qt::AlignCenter, label);
    }
  }

  // 指针
  const float na = 180.0f - dbmToNorm(needle_) * 180.0f;
  p.setPen(QPen(QColor(255, 235, 130), 2.5));
  p.drawLine(c, QPointF(c.x() + std::cos(na * M_PI / 180) * r * 0.95f,
                        c.y() - std::sin(na * M_PI / 180) * r * 0.95f));
  p.setBrush(QColor(220, 190, 90));
  p.drawEllipse(c, 4, 4);

  // 读数
  p.setPen(QColor(150, 230, 170));
  p.setFont(QFont("Consolas", 11, QFont::Bold));
  p.drawText(QRectF(c.x() - 90, c.y() - r * 0.34, 180, 20), Qt::AlignCenter,
             QString("S%1  %2 dBm")
                 .arg(dbmToNorm(dbm_) * 9.0f + (dbmToNorm(dbm_) > 0.75f
                                                    ? (dbmToNorm(dbm_) - 0.75f) * 240.0f
                                                    : 0.0f),
                      0, 'f', 1)
                 .arg(dbm_, 0, 'f', 0));
}

PoSwrWidget::PoSwrWidget(QWidget* parent) : QFrame(parent) {
  // 缩成同排右侧的小表：两条短横条，不再占半行宽
  setMinimumSize(180, 64);
  setMaximumSize(220, 80);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void PoSwrWidget::setPoSwr(float po_w, float swr) {
  po_w_ = po_w;
  swr_ = swr;
  update();
}

void PoSwrWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const int w = width(), h = height();
  const int top = (h - 42) / 2;  // 两条表纵向居中

  auto bar = [&](int y, const QString& name, float v, float vmax, float red_from,
                 const QString& unit) {
    p.setPen(QColor(190, 198, 208));
    p.setFont(QFont("Consolas", 9));
    p.drawText(QRectF(4, y, 46, 14), Qt::AlignVCenter, name);
    const float bw = w - 78;
    p.setBrush(QColor(20, 24, 30));
    p.setPen(QPen(QColor(80, 88, 100), 1));
    p.drawRect(QRectF(50, y + 1, bw, 12));
    const float frac = std::clamp(v / vmax, 0.0f, 1.0f);
    p.setBrush(v > red_from ? QColor(255, 90, 70) : QColor(90, 220, 120));
    p.drawRect(QRectF(51, y + 2, (bw - 2) * frac, 10));
    p.setPen(QColor(220, 226, 232));
    p.drawText(QRectF(w - 26 - 24, y, 52, 14), Qt::AlignVCenter,
               QString::number(v, 'f', v < 10 ? 1 : 0) + unit);
  };

  bar(top + 2, "Po", po_w_, 10.0f, 9.5f, "W");
  bar(top + 28, "SWR", swr_, 6.0f, 3.0f, "");
}
