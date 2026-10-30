// SPDX-License-Identifier: AGPL-3.0-or-later
// WSJT-X 接入测试：UDP loopback 收报文 + 工作台离屏 UI
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QThread>
#include <QUdpSocket>
#include <functional>

#include "app/wsjtx_client.h"
#include "app/wsjtx_logbook.h"
#include "app/wsjtx_panel.h"

namespace {

QApplication& uiApp() {
  static int argc = 1;
  static char name[] = "wsjtx-ui-tests";
  static char* argv[] = {name, nullptr};
  static QApplication app(argc, argv);
  return app;
}

bool waitUntil(const std::function<bool()>& pred, int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (!pred() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(5);
  }
  return pred();
}

QByteArray datagramOf(const radio::wsjtx::Message& message) {
  const auto wire = radio::wsjtx::encode(message);
  return QByteArray(reinterpret_cast<const char*>(wire.data()),
                    static_cast<int>(wire.size()));
}

}  // namespace

TEST_CASE("wsjtx: udp client receives status, decode and heartbeat", "[wsjtx][ui]") {
  uiApp();
  radio::app::WsjtxClient client;
  client.setClientId("radio-suite-test");
  QString error;
  REQUIRE(client.start("127.0.0.1", 0, &error));
  CHECK(error.isEmpty());
  CHECK(client.isListening());
  const quint16 port = client.port();
  REQUIRE(port != 0);

  int status_count = 0;
  int decode_count = 0;
  int heartbeat_count = 0;
  radio::wsjtx::Status got_status;
  radio::wsjtx::Decode got_decode;
  QString got_version;
  QObject::connect(&client, &radio::app::WsjtxClient::statusReceived,
                   [&](const radio::wsjtx::Status& status, const QString& id) {
                     ++status_count;
                     got_status = status;
                     CHECK(id == "WSJT-X");
                   });
  QObject::connect(&client, &radio::app::WsjtxClient::decodeReceived,
                   [&](const radio::wsjtx::Decode& decode, const QString&) {
                     ++decode_count;
                     got_decode = decode;
                   });
  QObject::connect(&client, &radio::app::WsjtxClient::heartbeatReceived,
                   [&](const QString&, const QString& version, const QString&) {
                     ++heartbeat_count;
                     got_version = version;
                   });

  QUdpSocket sender;
  radio::wsjtx::Status status;
  status.dial_frequency = 14'074'000;
  status.mode = "FT8";
  status.de_call = "N0CALL";
  status.de_grid = "PM01";
  status.decoding = true;
  REQUIRE(sender.writeDatagram(
              datagramOf(radio::wsjtx::Message{radio::wsjtx::MessageType::Status,
                                               "WSJT-X", status}),
              QHostAddress::LocalHost, port) > 0);

  radio::wsjtx::Decode decode;
  decode.time_ms = 12'345;
  decode.snr = -12;
  decode.delta_time = -0.3;
  decode.delta_frequency = 1500;
  decode.mode = "FT8";
  decode.message = "CQ N0CALL PM01";
  REQUIRE(sender.writeDatagram(
              datagramOf(radio::wsjtx::Message{radio::wsjtx::MessageType::Decode,
                                               "WSJT-X", decode}),
              QHostAddress::LocalHost, port) > 0);

  REQUIRE(sender.writeDatagram(
              datagramOf(radio::wsjtx::makeHeartbeat("WSJT-X", "2.7.0", "abc")),
              QHostAddress::LocalHost, port) > 0);

  REQUIRE(waitUntil([&] { return status_count == 1 && decode_count == 1 &&
                                heartbeat_count == 1; }));
  CHECK(got_status.dial_frequency == 14'074'000);
  CHECK(got_status.mode == "FT8");
  CHECK(got_status.de_call == "N0CALL");
  CHECK(got_status.decoding);
  CHECK(got_decode.time_ms == 12'345);
  CHECK(got_decode.snr == -12);
  CHECK((got_decode.delta_time > -0.31 && got_decode.delta_time < -0.29));
  CHECK(got_decode.delta_frequency == 1500);
  CHECK(got_decode.message == "CQ N0CALL PM01");
  CHECK(got_version == "2.7.0");

  // 非 WSJT-X 报文只报错，不产生事件
  int errors = 0;
  QObject::connect(&client, &radio::app::WsjtxClient::error,
                   [&](const QString&) { ++errors; });
  REQUIRE(sender.writeDatagram(QByteArray("\x01\x02\x03", 3),
                               QHostAddress::LocalHost, port) > 0);
  REQUIRE(waitUntil([&] { return errors == 1; }));
  CHECK(status_count == 1);
  CHECK(decode_count == 1);

  client.stop();
  CHECK_FALSE(client.isListening());
}

TEST_CASE("wsjtx: panel renders decodes, qso, wspr and adif", "[wsjtx][ui]") {
  uiApp();
  radio::app::WsjtxPanel panel;
  panel.startListening("127.0.0.1", 0);
  REQUIRE(panel.isListening());

  radio::wsjtx::Status status;
  status.dial_frequency = 14'074'000;
  status.mode = "FT8";
  status.de_call = "N0CALL";
  status.de_grid = "PM01";
  status.tx_enabled = true;
  status.tx_message = "CQ N0CALL PM01";
  panel.applyStatus(status, "WSJT-X");

  radio::wsjtx::Decode decode;
  decode.time_ms = 45'000;
  decode.snr = -8;
  decode.delta_time = 0.2;
  decode.delta_frequency = 1200;
  decode.mode = "FT8";
  decode.message = "CQ BG5GXO PM01";
  panel.addDecode(decode, "WSJT-X");

  radio::wsjtx::QsoLogged qso;
  qso.date_time_off = radio::wsjtx::DateTime::fromUtcMs(1'700'000'000'000);
  qso.dx_call = "BG5GXO";
  qso.dx_grid = "PM01";
  qso.tx_frequency = 14'074'000;
  qso.mode = "FT8";
  qso.report_sent = "-10";
  qso.report_received = "-12";
  qso.operator_call = "N0CALL";
  panel.addQso(qso);

  radio::wsjtx::WsprDecode wspr;
  wspr.time_ms = 90'000;
  wspr.snr = -20;
  wspr.frequency = 14'095'600;
  wspr.callsign = "BG5GXO";
  wspr.grid = "PM01";
  wspr.power_dbm = 37;
  panel.addWspr(wspr);

  panel.setAdif("<adif_ver:5>3.0.7\n<EOH>\n<call:6>BG5GXO<EOR>");
  panel.setMyGrid("PM01aa");
  panel.resize(1000, 640);
  panel.show();
  QApplication::processEvents();
  const QPixmap shot = panel.grab();
  CHECK_FALSE(shot.isNull());
  CHECK(shot.width() >= 900);
  CHECK(shot.height() >= 600);

  panel.stopListening();
  CHECK_FALSE(panel.isListening());
}

TEST_CASE("wsjtx: decode activation fills call, grid and absolute frequency",
          "[wsjtx][ui]") {
  uiApp();
  radio::app::WsjtxPanel panel;

  radio::wsjtx::Status status;
  status.dial_frequency = 14'074'000;
  status.mode = "FT8";
  panel.applyStatus(status, "WSJT-X");

  QString got_call, got_grid, got_mode;
  quint64 got_hz = 0;
  int fired = 0;
  QObject::connect(&panel, &radio::app::WsjtxPanel::decodeActivated,
                   [&](const QString& call, const QString& grid, quint64 hz,
                       const QString& mode) {
                     ++fired;
                     got_call = call;
                     got_grid = grid;
                     got_hz = hz;
                     got_mode = mode;
                   });

  radio::wsjtx::Decode cq;
  cq.mode = "FT8";
  cq.delta_frequency = 1200;
  cq.message = "CQ DX BG5GXO PM01";
  panel.addDecode(cq, "WSJT-X");
  CHECK(panel.activateDecode(0));
  CHECK(fired == 1);
  CHECK(got_call == "BG5GXO");
  CHECK(got_grid == "PM01");
  CHECK(got_hz == 14'074'000u + 1200u);
  CHECK(got_mode == "FT8");

  radio::wsjtx::Decode directed;
  directed.mode = "FT8";
  directed.delta_frequency = 500;
  directed.message = "N0CALL BG5GXO -12";
  panel.addDecode(directed, "WSJT-X");
  CHECK(panel.activateDecode(0));
  CHECK(fired == 2);
  CHECK(got_call == "BG5GXO");
  CHECK(got_grid.isEmpty());
  CHECK(got_hz == 14'074'000u + 500u);

  radio::wsjtx::Decode noise;
  noise.message = "TU 73";
  panel.addDecode(noise, "WSJT-X");
  CHECK_FALSE(panel.activateDecode(0));
  CHECK(fired == 2);
  CHECK_FALSE(panel.activateDecode(99));
  CHECK(fired == 2);

  // CQ 修饰词不限于 DX/TEST：POTA/SOTA/QRP 等（均不含数字）都该跳过
  radio::wsjtx::Decode pota;
  pota.mode = "FT8";
  pota.delta_frequency = 300;
  pota.message = "CQ POTA BG5GXO PM01RF";
  panel.addDecode(pota, "WSJT-X");
  CHECK(panel.activateDecode(0));
  CHECK(fired == 3);
  CHECK(got_call == "BG5GXO");
  CHECK(got_grid == "PM01RF");
  CHECK(got_hz == 14'074'000u + 300u);

  // RR73/RRR 是协议码不是呼号
  radio::wsjtx::Decode rr73;
  rr73.message = "BG5GXO RR73";
  panel.addDecode(rr73, "WSJT-X");
  CHECK_FALSE(panel.activateDecode(0));
  CHECK(fired == 3);

  // 负的 delta_frequency（低于刻度）也要落到绝对频率上
  radio::wsjtx::Decode negative;
  negative.mode = "FT8";
  negative.delta_frequency = -400;
  negative.message = "CQ BG5GXO PM01";
  panel.addDecode(negative, "WSJT-X");
  CHECK(panel.activateDecode(0));
  CHECK(got_hz == 14'074'000u - 400u);
}

TEST_CASE("wsjtx: logged qso maps onto the HamLog schema", "[wsjtx][ui]") {
  radio::wsjtx::QsoLogged logged;
  logged.date_time_off = radio::wsjtx::DateTime::fromUtcMs(1'700'000'000'000);
  logged.dx_call = "bg5gxo";
  logged.dx_grid = "PM01aa";
  logged.tx_frequency = 14'074'000;
  logged.mode = "FT8";
  logged.report_sent = "-10";
  logged.report_received = "-12";
  logged.operator_call = "N0CALL";

  const auto qso = radio::app::qsoFromWsjtx(logged);
  CHECK(qso.callsign == "BG5GXO");
  CHECK(qso.freq_mhz == 14.074);
  CHECK(qso.year == 2023);
  CHECK(qso.month == 11);
  CHECK(qso.day == 14);
  CHECK(qso.time_hhmm == "2213");
  CHECK(qso.mode == "FT8");
  CHECK(qso.rst_sent == "-10");
  CHECK(qso.rst_rcvd == "-12");
  CHECK(qso.device == "WSJT-X");
  CHECK(qso.remarks == QString("网格 PM01aa"));
}
