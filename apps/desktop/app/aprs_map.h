// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/aprs_map.h — APRS 台站地图（QPainter 滑杆瓦片 + 矢量叠加）
//
// 瓦片层可选：默认 OSM 模板，按 z/x/y 落盘缓存；无网络时退化为经纬网 +
// 台站符号，功能不依赖在线地图。台站/轨迹来自 radio::aprs::StationStore。
#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QWidget>

#include <string>

namespace radio::aprs {
class StationStore;
struct Station;
}  // namespace radio::aprs

namespace radio::app {

class AprsMapWidget : public QWidget {
  Q_OBJECT
 public:
  explicit AprsMapWidget(QWidget* parent = nullptr);

  void setStations(const radio::aprs::StationStore* store);
  void setCenter(double lat, double lon, int zoom = -1);
  void setSelected(const QString& key);
  void setTileUrl(const QString& url_template);  // 含 {z}/{x}/{y}
  void setMyPosition(bool has, double lat, double lon);
  void refresh();

  int zoom() const { return zoom_; }
  double centerLat() const { return center_lat_; }
  double centerLon() const { return center_lon_; }

 signals:
  void stationClicked(const QString& key);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  double worldSize() const;
  QPointF project(double lat, double lon) const;
  void unproject(const QPointF& world, double* lat, double* lon) const;
  QPixmap tile(int z, int x, int y);
  QString tileKey(int z, int x, int y) const;
  void requestTile(int z, int x, int y);
  void drawGraticule(QPainter& p);
  void drawStations(QPainter& p);
  QString stationKeyAt(const QPoint& pos) const;

  const radio::aprs::StationStore* store_ = nullptr;
  QString tile_url_ = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
  QNetworkAccessManager net_;
  QHash<QString, QPixmap> tiles_;
  QSet<QString> pending_;
  QString cache_dir_;

  double center_lat_ = 31.23;
  double center_lon_ = 121.47;
  int zoom_ = 9;
  bool follow_ = true;

  bool has_my_ = false;
  double my_lat_ = 0.0;
  double my_lon_ = 0.0;

  QString selected_;
  QHash<QString, QPointF> hit_;  // 最近一次绘制的屏幕位置（点击命中）

  bool dragging_ = false;
  QPoint press_pos_;
  QPointF press_center_world_;
  bool moved_ = false;
};

}  // namespace radio::app
