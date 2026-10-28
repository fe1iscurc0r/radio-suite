// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/wsjtx_client.h — WSJT-X UDP 组播/单播客户端
#pragma once

#include <QHostAddress>
#include <QMetaType>
#include <QObject>
#include <QString>

#include "wsjtx/wsjtx.h"

class QUdpSocket;

Q_DECLARE_METATYPE(radio::wsjtx::Status)
Q_DECLARE_METATYPE(radio::wsjtx::Decode)
Q_DECLARE_METATYPE(radio::wsjtx::QsoLogged)
Q_DECLARE_METATYPE(radio::wsjtx::WsprDecode)

namespace radio::app {

class WsjtxClient : public QObject {
  Q_OBJECT
 public:
  explicit WsjtxClient(QObject* parent = nullptr);
  ~WsjtxClient() override;

  // group 为组播地址（默认 224.0.0.1）；单播地址可用于测试
  bool start(const QString& group = QString::fromLatin1(radio::wsjtx::kDefaultGroup),
             quint16 port = radio::wsjtx::kDefaultPort, QString* error = nullptr);
  void stop();
  bool isListening() const;
  QString group() const { return group_; }
  quint16 port() const { return port_; }

  QString clientId() const { return client_id_; }
  void setClientId(const QString& id);

  bool send(const radio::wsjtx::Message& message);
  bool sendReply(const radio::wsjtx::Decode& decode, quint8 modifiers = 0);
  bool haltTx(bool auto_tx_only = false);
  bool freeText(const QString& text, bool send);
  bool replay();
  bool setLocation(const QString& grid);
  bool clearDecodes(int window = 0);

 signals:
  void statusReceived(const radio::wsjtx::Status& status, const QString& client_id);
  void decodeReceived(const radio::wsjtx::Decode& decode, const QString& client_id);
  void qsoLogged(const radio::wsjtx::QsoLogged& qso, const QString& client_id);
  void wsprDecode(const radio::wsjtx::WsprDecode& decode, const QString& client_id);
  void loggedAdif(const QString& adif, const QString& client_id);
  void heartbeatReceived(const QString& client_id, const QString& version,
                         const QString& revision);
  void clientClosed(const QString& client_id);
  void listeningChanged(bool listening);
  void error(const QString& message);

 private slots:
  void onReadyRead();

 private:
  QUdpSocket* socket_ = nullptr;
  QString group_ = QString::fromLatin1(radio::wsjtx::kDefaultGroup);
  quint16 port_ = radio::wsjtx::kDefaultPort;
  QString client_id_ = "radio-suite";
};

}  // namespace radio::app
