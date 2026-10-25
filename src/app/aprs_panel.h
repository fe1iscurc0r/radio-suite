// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/aprs_panel.h — APRS-IS 客户端面板（接收聚合 + 上行发送）
//
// APRSLocus（GPL-3.0）融合第二片：台站按呼号聚合（其 stations 列表语义）+
// 位置信标/消息上行（APRS-IS 通行码认证，公开算法）。接收侧 filter 可调；
// 发射侧（射频）按工单 P3 另议，此处仅 Internet 上行。
#pragma once

#include <QDateTime>
#include <QMap>
#include <QTcpSocket>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace radio::app {

struct AprsStation {
  QString last_coord;
  QString comment;
  QDateTime last_seen;
  int packets = 0;
};

class AprsPanel : public QWidget {
  Q_OBJECT
 public:
  explicit AprsPanel(QWidget* parent = nullptr);
  ~AprsPanel() override;

  void setLoginCall(const QString& call);  // HamLog settings 的 my_callsign
  bool isConnected() const { return sock_->state() == QTcpSocket::ConnectedState; }
  QString loginCall() const { return login_call_; }

 public slots:
  void toggleConnect();
  void sendBeacon();       // 位置信标上行（需真实呼号，N0CALL 禁发）
  void sendMessage();      // 文本消息上行（对话框）

 signals:
  void statusMessage(const QString& msg);

 private:
  void onConnected();
  void onReadyRead();
  void handleLine(const QString& raw);
  void refreshTable();
  void sendRaw(const QString& line);

  QTcpSocket* sock_ = nullptr;
  QTableWidget* table_ = nullptr;          // 台站聚合（呼号/坐标/包数/最后收到）
  QLabel* counter_ = nullptr;
  QPushButton* beacon_btn_ = nullptr;
  QPushButton* msg_btn_ = nullptr;
  QString login_call_ = "N0CALL";  // 无真实呼号时只收不发
  QString rx_buf_;
  int try_server_ = 0;
  QMap<QString, AprsStation> stations_;  // 呼号 → 聚合（滚动不再刷屏）
  int total_packets_ = 0;
};

}  // namespace radio::app
