// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · bus/rigctld_server.h — rigctld 服务器（WSJT-X/fldigi 即插）
//
// 协议语义与 dump_state 结构移植自 rigplane-core（MIT）src/rigplane/rigctld/
// {protocol,contract,handler}.py @ master（© KN4KYD），文件头按集大成规约标注。
// hamlib rigctld(1) wire 协议：TCP 4532，行式命令；GET 回值一行；SET 回 RPRT 0；
// 错误 RPRT -n；长命令带 \ 前缀；\chk_vfo→"0"。
// 首版命令集：f/F m/M v s q + \chk_vfo + \dump_state（WSJT-X 最小闭环）。
#pragma once

#include <QTcpServer>
#include <QObject>
#include <QMap>

#include "app/session.h"

namespace radio::bus {

class RigctldServer : public QObject {
  Q_OBJECT
 public:
  struct Config {
    uint16_t port = 4532;
    QString bind_addr = "127.0.0.1";
  };

  RigctldServer(radio::app::RadioSession& session, QObject* parent = nullptr);

  bool start(const Config& cfg, QString* error);
  uint16_t port() const { return server_ ? server_->serverPort() : 0; }

 private slots:
  void onNewConnection();
  void onClientReadyRead();
  void onClientDisconnected(QObject* sock);

 private:
  QByteArray handleLine(const QString& line, bool& close_conn);
  QByteArray dumpState() const;
  static int hamlibModeBit(const QString& name);
  static QString modeNameFromHamlib(const QString& name);

  radio::app::RadioSession& session_;
  QTcpServer* server_ = nullptr;
  QMap<QObject*, QByteArray> rx_buf_;
};

}  // namespace radio::bus
