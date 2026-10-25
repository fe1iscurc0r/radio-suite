// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/aprs_panel.h — APRS-IS 接收面板
//
// APRSLocus（GPL-3.0）融合第一片：位置报告解析设计参考其 lib/aprs_parse.dart
// （依 AGPLv3 §13 并入范围；本文件为独立实现的最小子集：源呼号 + 未压缩
// 经纬度 + 备注）。接收侧功能，发射侧按工单 P3 另议。
#pragma once

#include <QListWidget>
#include <QTcpSocket>
#include <QWidget>

class QPushButton;

namespace radio::app {

class AprsPanel : public QWidget {
  Q_OBJECT
 public:
  explicit AprsPanel(QWidget* parent = nullptr);
  ~AprsPanel() override;

  void setLoginCall(const QString& call);  // HamLog settings 的 my_callsign
  bool isConnected() const { return sock_->state() == QTcpSocket::ConnectedState; }

 public slots:
  void toggleConnect();

 signals:
  void statusMessage(const QString& msg);

 private:
  void onConnected();
  void onReadyRead();
  void appendLine(const QString& raw);  // 解析一行 APRS 数据

  QTcpSocket* sock_ = nullptr;
  QListWidget* view_ = nullptr;
  QString login_call_ = "N0CALL";  // pass -1 只收不发
  QString rx_buf_;
  int try_server_ = 0;      // kServers 下标（国内优先回落国际）
  int station_count_ = 0;   // 累计收到台站数
  static constexpr int kMaxRows = 60;
};

}  // namespace radio::app
