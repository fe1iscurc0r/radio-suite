// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/aprs_map.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStandardPaths>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "aprs/store.h"

namespace radio::app {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kTileSize = 256;

double clampLat(double lat) {
  return std::clamp(lat, -85.05112878, 85.05112878);
}

double lonToWorldX(double lon, double world) {
  return (lon + 180.0) / 360.0 * world;
}

double latToWorldY(double lat, double world) {
  const double r = clampLat(lat) * kPi / 180.0;
  const double y = std::log(std::tan(r) + 1.0 / std::cos(r));
  return (1.0 - y / kPi) / 2.0 * world;
}

double worldXToLon(double x, double world) {
  return x / world * 360.0 - 180.0;
}

double worldYToLat(double y, double world) {
  const double n = kPi * (1.0 - 2.0 * y / world);
  return 180.0 / kPi * std::atan(std::sinh(n));
}

}  // namespace

AprsMapWidget::AprsMapWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(320, 220);
  setMouseTracking(true);
  setAutoFillBackground(false);
  QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (base.isEmpty()) base = QDir::tempPath() + "/radio-suite-tiles";
  cache_dir_ = QDir(base).filePath("aprs-tiles");
  QDir().mkpath(cache_dir_);
  connect(&net_, &QNetworkAccessManager::finished, this,
          [this](QNetworkReply* reply) {
            const QString key = reply->property("tile_key").toString();
            const QString path = reply->property("tile_path").toString();
            pending_.remove(key);
            if (reply->error() == QNetworkReply::NoError) {
              const QByteArray data = reply->readAll();
              QPixmap pm;
              if (pm.loadFromData(data) && !path.isEmpty()) {
                QFile f(path);
                if (f.open(QIODevice::WriteOnly)) f.write(data);
                if (tiles_.size() > 600) tiles_.clear();
                tiles_.insert(key, pm);
                update();
              }
            }
            reply->deleteLater();
          });
}

void AprsMapWidget::setStations(const radio::aprs::StationStore* store) {
  store_ = store;
  update();
}

void AprsMapWidget::setCenter(double lat, double lon, int zoom) {
  center_lat_ = clampLat(lat);
  center_lon_ = lon;
  if (zoom > 0) zoom_ = std::clamp(zoom, 2, 18);
  follow_ = true;
  update();
}

void AprsMapWidget::setSelected(const QString& key) {
  selected_ = key;
  update();
}

void AprsMapWidget::setTileUrl(const QString& url_template) {
  if (url_template.trimmed().isEmpty()) return;
  tile_url_ = url_template.trimmed();
  tiles_.clear();
  update();
}

void AprsMapWidget::setMyPosition(bool has, double lat, double lon) {
  has_my_ = has;
  my_lat_ = lat;
  my_lon_ = lon;
  update();
}

void AprsMapWidget::refresh() { update(); }

double AprsMapWidget::worldSize() const {
  return static_cast<double>(kTileSize) * std::pow(2.0, zoom_);
}

QPointF AprsMapWidget::project(double lat, double lon) const {
  const double world = worldSize();
  return {lonToWorldX(lon, world), latToWorldY(lat, world)};
}

void AprsMapWidget::unproject(const QPointF& world, double* lat,
                              double* lon) const {
  const double size = worldSize();
  if (lon) *lon = worldXToLon(world.x(), size);
  if (lat) *lat = worldYToLat(world.y(), size);
}

QString AprsMapWidget::tileKey(int z, int x, int y) const {
  return QString("%1/%2/%3").arg(z).arg(x).arg(y);
}

QPixmap AprsMapWidget::tile(int z, int x, int y) {
  const QString key = tileKey(z, x, y);
  auto it = tiles_.find(key);
  if (it != tiles_.end()) return *it;
  const QString path = QDir(cache_dir_).filePath(
      QString("%1/%2/%3.png").arg(z).arg(x).arg(y));
  QPixmap pm;
  if (QFile::exists(path) && pm.load(path)) {
    if (tiles_.size() > 600) tiles_.clear();
    tiles_.insert(key, pm);
    return pm;
  }
  requestTile(z, x, y);
  return {};
}

void AprsMapWidget::requestTile(int z, int x, int y) {
  if (qEnvironmentVariableIsSet("RADIO_SUITE_APRS_TILES") &&
      qEnvironmentVariable("RADIO_SUITE_APRS_TILES") == "off")
    return;
  const QString key = tileKey(z, x, y);
  if (pending_.contains(key)) return;
  QString url = tile_url_;
  url.replace("{z}", QString::number(z));
  url.replace("{x}", QString::number(x));
  url.replace("{y}", QString::number(y));
  const QString path = QDir(cache_dir_).filePath(
      QString("%1/%2/%3.png").arg(z).arg(x).arg(y));
  QDir().mkpath(QFileInfo(path).absolutePath());
  QNetworkRequest req{QUrl(url)};
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setHeader(QNetworkRequest::UserAgentHeader, "radio-suite/0.1 (APRS map)");
  pending_.insert(key);
  auto* reply = net_.get(req);
  reply->setProperty("tile_key", key);
  reply->setProperty("tile_path", path);
}

void AprsMapWidget::drawGraticule(QPainter& p) {
  const double steps[] = {30.0, 10.0, 5.0, 2.0, 1.0, 0.5, 0.2, 0.1, 0.05};
  const double step = steps[std::clamp(zoom_ - 2, 0, 8)];
  p.setPen(QPen(QColor(70, 110, 130, 120), 1, Qt::DashLine));
  const QPointF tl = project(center_lat_, center_lon_) -
                     QPointF(width() / 2.0, height() / 2.0);
  double lon = std::floor(-180.0 / step) * step;
  qreal last_label_x = -1e9;
  for (; lon <= 180.0; lon += step) {
    const double x = project(0.0, lon).x() - tl.x();
    if (x < -1 || x > width() + 1) continue;
    p.drawLine(QPointF(x, 0), QPointF(x, height()));
    const QString label = QString::number(lon, 'f', step < 1 ? 1 : 0) + "°";
    // 相邻标签会首尾相接（0.1° 步进时约 59px），按上一枚的宽度隔一枚画。
    if (x - last_label_x >= p.fontMetrics().horizontalAdvance(label) + 10) {
      p.setPen(QColor(120, 160, 180, 180));
      p.drawText(QPointF(x + 3, height() - 8), label);
      last_label_x = x;
      p.setPen(QPen(QColor(70, 110, 130, 120), 1, Qt::DashLine));
    }
  }
  double lat = std::floor(-85.0 / step) * step;
  for (; lat <= 85.0; lat += step) {
    const double y = project(lat, 0.0).y() - tl.y();
    if (y < -1 || y > height() + 1) continue;
    p.drawLine(QPointF(0, y), QPointF(width(), y));
    p.setPen(QColor(120, 160, 180, 180));
    // 最底一条纬度刻度与经度标签同高（y≈height()-8），会叠字，只保留网格线。
    if (y >= 20 && y <= height() - 20)
      p.drawText(QPointF(4, y - 3),
                 QString::number(lat, 'f', step < 1 ? 1 : 0) + "°");
    p.setPen(QPen(QColor(70, 110, 130, 120), 1, Qt::DashLine));
  }
}

QString AprsMapWidget::stationKeyAt(const QPoint& pos) const {
  QString best;
  double best_dist = 16.0;
  for (auto it = hit_.begin(); it != hit_.end(); ++it) {
    const double d = std::hypot(it.value().x() - pos.x(), it.value().y() - pos.y());
    if (d < best_dist) {
      best_dist = d;
      best = it.key();
    }
  }
  return best;
}

void AprsMapWidget::drawStations(QPainter& p) {
  hit_.clear();
  if (store_ == nullptr) return;
  const QPointF tl = project(center_lat_, center_lon_) -
                     QPointF(width() / 2.0, height() / 2.0);
  const double world = worldSize();

  // 选中台站轨迹
  if (!selected_.isEmpty()) {
    const auto* st = store_->find(selected_.toStdString());
    if (st != nullptr && st->track.size() > 1) {
      QPainterPath path;
      bool first = true;
      for (const auto& tp : st->track) {
        double x = lonToWorldX(tp.pos.lon, world) - tl.x();
        double y = latToWorldY(tp.pos.lat, world) - tl.y();
        if (!first && std::fabs(x - path.currentPosition().x()) > world / 2)
          continue;  // 跨 180° 不连笔
        if (first) {
          path.moveTo(x, y);
          first = false;
        } else {
          path.lineTo(x, y);
        }
      }
      p.setPen(QPen(QColor(255, 210, 90, 160), 1.6));
      p.drawPath(path);
    }
  }

  p.setFont(QFont("Segoe UI", 8));
  for (const auto& [key, st] : store_->all()) {
    if (!st.hasPosition()) continue;
    const double x = lonToWorldX(st.pos.lon, world) - tl.x();
    const double y = latToWorldY(st.pos.lat, world) - tl.y();
    if (x < -20 || x > width() + 20 || y < -20 || y > height() + 20) continue;
    const QString qkey = QString::fromStdString(st.key);
    hit_.insert(qkey, QPointF(x, y));

    const bool sel = qkey == selected_;
    QColor fill = st.is_object ? QColor(0xd0, 0x6c, 0xe0)
                              : QColor(0x4d, 0xd0, 0xe0);
    if (!st.alive) fill = QColor(150, 150, 150);
    if (st.messaging) {
      p.setPen(QPen(QColor(120, 255, 140), 1));
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(QPointF(x, y), 8, 8);
    }
    p.setPen(QPen(sel ? QColor(255, 230, 120) : QColor(10, 20, 30), sel ? 2 : 1));
    p.setBrush(fill);
    if (st.is_object) {
      QPolygonF dia;
      dia << QPointF(x, y - 6) << QPointF(x + 6, y) << QPointF(x, y + 6)
          << QPointF(x - 6, y);
      p.drawPolygon(dia);
    } else {
      p.drawEllipse(QPointF(x, y), 4.5, 4.5);
    }
    if (sel || zoom_ >= 10) {
      p.setPen(QColor(230, 240, 245));
      p.drawText(QPointF(x + 8, y - 5), qkey);
    }
  }

  if (has_my_) {
    const double x = lonToWorldX(my_lon_, world) - tl.x();
    const double y = latToWorldY(my_lat_, world) - tl.y();
    p.setPen(QPen(QColor(255, 90, 90), 2));
    p.drawLine(QPointF(x - 7, y), QPointF(x + 7, y));
    p.drawLine(QPointF(x, y - 7), QPointF(x, y + 7));
    p.drawEllipse(QPointF(x, y), 6, 6);
  }
}

void AprsMapWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(8, 18, 28));

  const double world = worldSize();
  const int count = static_cast<int>(std::lround(world / kTileSize));
  const QPointF center = project(center_lat_, center_lon_);
  const double left = center.x() - width() / 2.0;
  const double top = center.y() - height() / 2.0;
  const int x0 = static_cast<int>(std::floor(left / kTileSize));
  const int y0 = static_cast<int>(std::floor(top / kTileSize));
  const int nx = width() / kTileSize + 2;
  const int ny = height() / kTileSize + 2;
  for (int ty = y0; ty <= y0 + ny; ++ty) {
    if (ty < 0 || ty >= count) continue;
    for (int tx = x0; tx <= x0 + nx; ++tx) {
      int wrapped = ((tx % count) + count) % count;
      const QPixmap pm = tile(zoom_, wrapped, ty);
      const double sx = tx * kTileSize - left;
      const double sy = ty * kTileSize - top;
      if (!pm.isNull())
        p.drawPixmap(QRectF(sx, sy, kTileSize, kTileSize), pm,
                     QRectF(0, 0, kTileSize, kTileSize));
    }
  }

  drawGraticule(p);
  drawStations(p);

  p.setPen(QColor(170, 200, 215));
  p.setFont(QFont("Segoe UI", 8));
  const QString hud = QString("%1 台站 · z%2 · %3")
                          .arg(store_ ? static_cast<int>(store_->size()) : 0)
                          .arg(zoom_)
                          .arg(follow_ ? "跟随" : "自由");
  p.drawText(8, 16, hud);
  p.setPen(QColor(140, 160, 170, 200));
  // 版权放右上：底部一行被经度刻度占着，左下会和第一枚经度标签叠字。
  const QString credit = "© OpenStreetMap contributors";
  p.drawText(width() - p.fontMetrics().horizontalAdvance(credit) - 8, 16, credit);
}

void AprsMapWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) return;
  dragging_ = true;
  moved_ = false;
  press_pos_ = event->pos();
  press_center_world_ = project(center_lat_, center_lon_);
}

void AprsMapWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!dragging_) return;
  const QPoint delta = event->pos() - press_pos_;
  if (delta.manhattanLength() > 3) moved_ = true;
  const QPointF world = press_center_world_ -
                        QPointF(delta.x(), delta.y());
  double lat = 0.0, lon = 0.0;
  unproject(world, &lat, &lon);
  center_lat_ = clampLat(lat);
  center_lon_ = lon;
  follow_ = false;
  update();
}

void AprsMapWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) return;
  dragging_ = false;
  if (!moved_) {
    const QString key = stationKeyAt(event->pos());
    if (!key.isEmpty()) {
      selected_ = key;
      emit stationClicked(key);
      update();
    }
  }
}

void AprsMapWidget::wheelEvent(QWheelEvent* event) {
  const double steps = event->angleDelta().y() / 120.0;
  if (steps == 0.0) return;
  const QPoint pos = event->position().toPoint();
  const QPointF tl = project(center_lat_, center_lon_) -
                     QPointF(width() / 2.0, height() / 2.0);
  double anchor_lat = 0.0, anchor_lon = 0.0;
  unproject(tl + QPointF(pos), &anchor_lat, &anchor_lon);
  const int old_zoom = zoom_;
  zoom_ = std::clamp(zoom_ + (steps > 0 ? 1 : -1), 2, 18);
  if (zoom_ == old_zoom) return;
  const double ax = lonToWorldX(anchor_lon, worldSize());
  const double ay = latToWorldY(anchor_lat, worldSize());
  unproject(QPointF(ax - (pos.x() - width() / 2.0),
                    ay - (pos.y() - height() / 2.0)),
            &center_lat_, &center_lon_);
  center_lat_ = clampLat(center_lat_);
  follow_ = false;
  update();
}

}  // namespace radio::app
