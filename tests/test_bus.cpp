// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTcpSocket>
#include <memory>

#include "bus/bus_server.h"
#include "core/transport_mock.h"
#include "qt_test_app.h"

namespace {

void pumpEvents() {
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  QThread::msleep(1);
}

struct HttpResponse {
  int status = 0;
  QByteArray body;
};

HttpResponse httpGet(uint16_t port, const QByteArray& target,
                     const QByteArray& extra_headers = {}) {
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, port);
  QElapsedTimer timer;
  timer.start();
  while (socket.state() != QAbstractSocket::ConnectedState && timer.elapsed() < 2000)
    pumpEvents();
  REQUIRE(socket.state() == QAbstractSocket::ConnectedState);
  const auto request = QByteArray("GET ") + target +
                       " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n" +
                       extra_headers + "\r\n";
  REQUIRE(socket.write(request) == request.size());
  timer.restart();
  while (socket.state() != QAbstractSocket::UnconnectedState && timer.elapsed() < 2000)
    pumpEvents();
  const auto raw = socket.readAll();
  const int split = raw.indexOf("\r\n\r\n");
  REQUIRE(split >= 0);
  HttpResponse response;
  response.status = raw.left(raw.indexOf("\r\n")).split(' ').value(1).toInt();
  response.body = raw.mid(split + 4);
  return response;
}

struct BusFixture {
  explicit BusFixture(QString token = {}) {
    qtTestApp();
    transport = std::make_shared<radio::core::MockTransport>();
    session = std::make_unique<radio::app::RadioSession>(transport);
    session->start();
    server = std::make_unique<radio::bus::BusServer>(*session);
    radio::bus::BusServer::Config cfg;
    cfg.port = 0;
    cfg.token = token;
    QString error;
    REQUIRE(server->start(cfg, &error));
  }

  std::shared_ptr<radio::core::MockTransport> transport;
  std::unique_ptr<radio::app::RadioSession> session;
  std::unique_ptr<radio::bus::BusServer> server;
};

}  // namespace

TEST_CASE("bus serves live RadioSession state over HTTP", "[bus][http]") {
  BusFixture f;
  REQUIRE(f.session->tuneTo(7'100'000));
  QElapsedTimer timer;
  timer.start();
  while (f.session->store().snapshot().freq_hz != 7'100'000 &&
         timer.elapsed() < 2000)
    pumpEvents();
  REQUIRE(f.session->store().snapshot().freq_hz == 7'100'000);
  const auto response = httpGet(f.server->port(), "/api/state");
  REQUIRE(response.status == 200);
  const auto doc = QJsonDocument::fromJson(response.body);
  REQUIRE(doc.isObject());
  CHECK(doc.object().value("freq_hz").toInteger() == 7'100'000);
  CHECK(doc.object().value("mode").toString() == "USB");
}

TEST_CASE("bus spectrum endpoint emits generated DSP frames", "[bus][http]") {
  BusFixture f;
  QElapsedTimer timer;
  timer.start();
  while (f.server->spectrumSeq() == 0 && timer.elapsed() < 2000) pumpEvents();
  REQUIRE(f.server->spectrumSeq() > 0);
  const auto response = httpGet(f.server->port(), "/api/spectrum");
  REQUIRE(response.status == 200);
  const auto doc = QJsonDocument::fromJson(response.body);
  REQUIRE(doc.isObject());
  CHECK(doc.object().value("frame_seq").toInteger() >= 1);
  CHECK(doc.object().value("bin_hz").toDouble() > 0.0);
  CHECK_FALSE(doc.object().value("power_db").toArray().isEmpty());
}

TEST_CASE("bus enforces token by query or header", "[bus][http][security]") {
  BusFixture f("secret");
  CHECK(httpGet(f.server->port(), "/api/state").status == 401);
  CHECK(httpGet(f.server->port(), "/api/state?token=wrong").status == 401);
  CHECK(httpGet(f.server->port(), "/api/state?token=secret").status == 200);
  CHECK(httpGet(f.server->port(), "/api/state",
                "X-Radio-Token: secret\r\n").status == 200);
}

TEST_CASE("bus refuses public bind without token", "[bus][security]") {
  BusFixture f;
  radio::bus::BusServer public_server(*f.session);
  radio::bus::BusServer::Config cfg;
  cfg.port = 0;
  cfg.bind_addr = "0.0.0.0";
  QString error;
  CHECK_FALSE(public_server.start(cfg, &error));
  CHECK_FALSE(error.isEmpty());
}
