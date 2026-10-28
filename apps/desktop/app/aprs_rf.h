// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/aprs_rf.h — KISS TNC 链路（TCP 或串口，Dire Wolf / LoRa APRS）
//
// 只做字节搬运：TCP/串口 → KISS 解帧 → AX.25 帧（信号）；发送则 KISS 封帧。
// 串口依赖 Qt6::SerialPort，未安装时自动降级为仅 TCP。
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTcpSocket>

#include <cstdint>
#include <span>

#ifdef RADIO_SUITE_HAVE_SERIALPORT
#include <QSerialPort>
#endif

#include "aprs/aprs.h"

namespace radio::app {

class KissLink : public QObject {
  Q_OBJECT
 public:
  explicit KissLink(QObject* parent = nullptr);

  void connectTcp(const QString& host, quint16 port);
#ifdef RADIO_SUITE_HAVE_SERIALPORT
  void connectSerial(const QString& port_name, int baud = 9600,
                     int kiss_port = 0);
#endif
  void disconnectLink();
  bool isConnected() const;
  QString describe() const { return desc_; }

  bool sendAx25(std::span<const uint8_t> frame);

 signals:
  void ax25Frame(const QByteArray& frame);
  void statusChanged(const QString& text);

 private:
  void onBytes(const QByteArray& bytes);
  void fail(const QString& why);

  QTcpSocket* tcp_ = nullptr;
#ifdef RADIO_SUITE_HAVE_SERIALPORT
  QSerialPort* serial_ = nullptr;
#endif
  radio::aprs::KissDecoder decoder_;
  QString desc_;
  uint8_t kiss_port_ = 0;
};

}  // namespace radio::app
