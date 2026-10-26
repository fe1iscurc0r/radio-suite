// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QThread>
#include <QTcpSocket>
#include <memory>

#include "bus/rigctld_server.h"
#include "core/transport_mock.h"
#include "qt_test_app.h"

namespace {

void pumpEvents() {
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  QThread::msleep(1);
}

void connectClient(QTcpSocket& socket, uint16_t port) {
  socket.connectToHost(QHostAddress::LocalHost, port);
  QElapsedTimer timer;
  timer.start();
  while (socket.state() != QAbstractSocket::ConnectedState && timer.elapsed() < 2000)
    pumpEvents();
  REQUIRE(socket.state() == QAbstractSocket::ConnectedState);
}

QByteArray exchange(QTcpSocket& socket, const QByteArray& line, int newlines = 1) {
  REQUIRE(socket.write(line + '\n') == line.size() + 1);
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 2000) {
    pumpEvents();
    const auto pending = socket.peek(65536);
    if (pending.count('\n') >= newlines) return socket.readAll();
  }
  return socket.readAll();
}

struct RigctldFixture {
  RigctldFixture() {
    qtTestApp();
    transport = std::make_shared<radio::core::MockTransport>();
    session = std::make_unique<radio::app::RadioSession>(transport);
    session->start();
    server = std::make_unique<radio::bus::RigctldServer>(*session);
    QString error;
    radio::bus::RigctldServer::Config cfg;
    cfg.port = 0;
    REQUIRE(server->start(cfg, &error));
    connectClient(socket, server->port());
  }

  std::shared_ptr<radio::core::MockTransport> transport;
  std::unique_ptr<radio::app::RadioSession> session;
  std::unique_ptr<radio::bus::RigctldServer> server;
  QTcpSocket socket;
};

}  // namespace

TEST_CASE("rigctld sets and gets frequency through RadioSession", "[rigctld][tcp]") {
  RigctldFixture f;
  CHECK(exchange(f.socket, "f") == "14270000\n");
  CHECK(exchange(f.socket, "F 7100000") == "RPRT 0\n");
  QElapsedTimer timer;
  timer.start();
  while (f.session->store().snapshot().freq_hz != 7'100'000 &&
         timer.elapsed() < 2000)
    pumpEvents();
  REQUIRE(f.session->store().snapshot().freq_hz == 7'100'000);
  CHECK(exchange(f.socket, "f") == "7100000\n");
  CHECK(exchange(f.socket, "F 1000") == "RPRT -1\n");
  CHECK(exchange(f.socket, "f") == "7100000\n");
}

TEST_CASE("rigctld mode commands use production CI-V path", "[rigctld][tcp]") {
  RigctldFixture f;
  CHECK(exchange(f.socket, "M FM 10000") == "RPRT 0\n");
  QElapsedTimer timer;
  timer.start();
  while (f.session->store().snapshot().mode != radio::core::civ::Mode::FM &&
         timer.elapsed() < 2000)
    pumpEvents();
  REQUIRE(f.session->store().snapshot().mode == radio::core::civ::Mode::FM);
  CHECK(exchange(f.socket, "m", 2) == "FM\n10000\n");
  CHECK(exchange(f.socket, "M NOT_A_MODE 2400") == "RPRT -1\n");
  CHECK(exchange(f.socket, "m", 2) == "FM\n10000\n");
}

TEST_CASE("rigctld supports rigplane observation polling", "[rigctld][tcp]") {
  RigctldFixture f;
  CHECK(exchange(f.socket, "\\chk_vfo") == "0\n");
  CHECK(exchange(f.socket, "v") == "VFOA\n");
  CHECK(exchange(f.socket, "u NB") == "0\n");
  CHECK(exchange(f.socket, "l ATT") == "0\n");
  CHECK(exchange(f.socket, "l STRENGTH") != "RPRT -1\n");
  CHECK(exchange(f.socket, "t") == "0\n");
  CHECK(exchange(f.socket, "get_powerstat") == "1\n");
  CHECK(exchange(f.socket, "get_split_freq") == "0\n");
  CHECK(exchange(f.socket, "f VFOB") == "14270000\n");
}
