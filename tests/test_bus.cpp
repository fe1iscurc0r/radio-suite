// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTcpSocket>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>

#include "bus/bus_server.h"
#include "core/transport_mock.h"
#include "qt_test_app.h"

// Catch2 不认识 QString，默认把每个 QChar 当容器元素打成一串 {?}——
// 变异对照时「相位报错了」这种失败根本读不出报的是什么。
namespace Catch {
template <>
struct StringMaker<QString> {
  static std::string convert(const QString& s) { return s.toStdString(); }
};
}  // namespace Catch

namespace {

void pumpEvents() {
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  QThread::msleep(1);
}

struct HttpResponse {
  int status = 0;
  QByteArray body;
  QByteArray head;  // 状态行 + 响应头，验缓存头这类东西要用
};

HttpResponse parseReply(const QByteArray& raw) {
  HttpResponse response;
  const int split = raw.indexOf("\r\n\r\n");
  REQUIRE(split >= 0);
  response.head = raw.left(split);
  response.status =
      response.head.left(response.head.indexOf("\r\n")).split(' ').value(1).toInt();
  response.body = raw.mid(split + 4);
  return response;
}

// 读到一个完整响应就收手，别等对端关连接。QHttpServer 在 Connection: close 下
// 并不及时断开，原来「等到 UnconnectedState」每次请求都要耗满 2 s 超时：
// 两击门禁的确认窗只有 5 s，中间夹几次 GET 就把窗口耗光，用例会假红。
HttpResponse readResponse(QTcpSocket& socket) {
  QByteArray raw;
  QElapsedTimer timer;
  timer.start();
  qsizetype body_end = -1;
  bool until_close = false;
  while (timer.elapsed() < 2000) {
    pumpEvents();
    raw += socket.readAll();
    const int split = raw.indexOf("\r\n\r\n");
    if (split >= 0 && body_end < 0 && !until_close) {
      const QByteArray head = raw.left(split).toLower();
      const int cl = head.indexOf("content-length:");
      if (cl < 0) {
        until_close = true;
      } else {
        const int eol = head.indexOf("\r\n", cl);
        const int stop = eol < 0 ? head.size() : eol;
        body_end = split + 4 + head.mid(cl + 15, stop - cl - 15).trimmed().toLongLong();
      }
    }
    if (body_end >= 0 && raw.size() >= body_end) break;
    if (until_close && socket.state() == QAbstractSocket::UnconnectedState) break;
  }
  socket.abort();
  REQUIRE_FALSE(raw.isEmpty());
  return parseReply(raw);
}

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
  return readResponse(socket);
}

// POST 这条路以前一条用例都没有——而它是唯一能让电台发射的出口（PWA 与 MCP 都走它）。
// 只读取样的用例证明不了门禁，必须把「按一次不发射」「发一帧还是发两帧」钉住。
HttpResponse httpPost(uint16_t port, const QByteArray& target,
                      const QByteArray& jsonBody,
                      const QByteArray& extra_headers = {}) {
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, port);
  QElapsedTimer timer;
  timer.start();
  while (socket.state() != QAbstractSocket::ConnectedState && timer.elapsed() < 2000)
    pumpEvents();
  REQUIRE(socket.state() == QAbstractSocket::ConnectedState);
  const auto request =
      QByteArray("POST ") + target +
      " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n" +
      extra_headers + "Content-Length: " +
      QByteArray::number(jsonBody.size()) + "\r\nConnection: close\r\n\r\n" +
      jsonBody;
  REQUIRE(socket.write(request) == request.size());
  return readResponse(socket);
}

QJsonObject jsonResult(const HttpResponse& response) {
  REQUIRE(response.status == 200);
  const auto doc = QJsonDocument::fromJson(response.body);
  REQUIRE(doc.isObject());
  return doc.object().value("result").toObject();
}

QJsonObject jsonBody(const HttpResponse& response) {
  const auto doc = QJsonDocument::fromJson(response.body);
  REQUIRE(doc.isObject());
  return doc.object();
}

// 数「一个动作把帧发了几遍」。mock 的状态看不出发了两遍设频（第二次只是重发同值），
// 所以拿一层装饰器把 send() 按 opcode 计数。opcode 取自 civ.h，别手抄：
// 设频是 0x05、PTT 是 0x1C（不是 0x00/0x13，抄错了整组用例都在数空气）。
class CountingTransport final : public radio::core::ICivTransport {
 public:
  explicit CountingTransport(std::shared_ptr<radio::core::MockTransport> inner)
      : inner_(std::move(inner)) {
    inner_->setFrameCallback(
        [this](std::span<const uint8_t> f) { emitFrame(f); });
  }

  bool open(std::string* error) override { return inner_->open(error); }
  void close() override { inner_->close(); }
  bool isOpen() const override { return inner_->isOpen(); }
  bool send(std::span<const uint8_t> frame) override {
    if (frame.size() >= 6) ++sent_by_cmd_[frame[4]];
    // PTT 写帧形状 FE FE to from 1C 00 <01|00> FD；读帧只到子命令，别混进来
    if (frame.size() >= 8 && frame[4] == radio::core::civ::kCmdPtt &&
        frame[5] == 0x00)
      ++ptt_writes_[frame[6] == 0x01];
    return inner_->send(frame);
  }
  size_t poll() override { return inner_->poll(); }

  int count(uint8_t cmd) const {
    const auto it = sent_by_cmd_.find(cmd);
    return it == sent_by_cmd_.end() ? 0 : it->second;
  }
  int pttWrites(bool on) const {
    const auto it = ptt_writes_.find(on);
    return it == ptt_writes_.end() ? 0 : it->second;
  }

 private:
  std::shared_ptr<radio::core::MockTransport> inner_;
  std::map<uint8_t, int> sent_by_cmd_;
  std::map<bool, int> ptt_writes_;
};

struct CountingBusFixture {
  CountingBusFixture() {
    qtTestApp();
    mock = std::make_shared<radio::core::MockTransport>();
    transport = std::make_shared<CountingTransport>(mock);
    session = std::make_unique<radio::app::RadioSession>(transport);
    session->start();
    server = std::make_unique<radio::bus::BusServer>(*session);
    radio::bus::BusServer::Config cfg;
    cfg.port = 0;
    QString error;
    REQUIRE(server->start(cfg, &error));
  }

  std::shared_ptr<radio::core::MockTransport> mock;
  std::shared_ptr<CountingTransport> transport;
  std::unique_ptr<radio::app::RadioSession> session;
  std::unique_ptr<radio::bus::BusServer> server;
};

// 状态是异步回来的：POST 之后要 pump 到 mock 的应答进会话，才能断言 sim()/snapshot()
void settle(const std::function<bool()>& done, int timeout_ms = 2000) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < timeout_ms) pumpEvents();
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

// SSE 这条路由 2026-11-03 落地时只写了代码没有用例，且那份写法根本编不过
// （把 QHttpServerResponder* 当 SseClient* 塞进 QSharedPointer）。这条用例把
// 真正的风险点钉住：responder 被 move 进客户端表之后，后续广播还得写得出帧。
TEST_CASE("bus streams spectrum frames over SSE to a kept-open client",
          "[bus][sse]") {
  BusFixture f;
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, f.server->port());
  QElapsedTimer timer;
  timer.start();
  while (socket.state() != QAbstractSocket::ConnectedState && timer.elapsed() < 2000)
    pumpEvents();
  REQUIRE(socket.state() == QAbstractSocket::ConnectedState);

  const QByteArray request =
      "GET /api/spectrum/stream HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Accept: text/event-stream\r\n\r\n";
  REQUIRE(socket.write(request) == request.size());

  QByteArray raw;
  timer.restart();
  while (!raw.contains("event: hello") && timer.elapsed() < 2000) {
    pumpEvents();
    raw += socket.readAll();
  }
  REQUIRE(raw.contains("event: hello"));
  CHECK(raw.contains("text/event-stream"));
  // 这一条才是浏览器真正看得见的东西：SSE 必须是 chunked 流。写成
  // responder.write(data, mime) 会带 content-length: 23，裸 socket 照样能读到后面的
  // 帧（所以上面那条 power_db 断言抓不到），但 EventSource 按 content-length 读完
  // 第一段就判定流结束、断开重连，频谱一帧都进不了 JS。
  const QByteArray lowered = raw.toLower();  // QByteArray::contains 没有大小写参数
  CHECK(lowered.contains("transfer-encoding: chunked"));
  CHECK_FALSE(lowered.contains("content-length"));

  // spec_timer_ 每 66 ms 广播一次；能收到 power_db 就说明长期持有的 responder 有效
  timer.restart();
  while (!raw.contains("power_db") && timer.elapsed() < 2000) {
    pumpEvents();
    raw += socket.readAll();
  }
  CHECK(raw.contains("power_db"));
  socket.abort();
}

// PWA 静态件必须 no-store。总线跑在开发机上，没缓存头时浏览器按启发式缓存旧
// app.js——改完前端同端口刷新还在跑旧脚本，SSE 那次就是这么被骗的（顶层导航
// 加 ignoreCache 都换不掉子资源）。这条用例钉住响应头，别让它悄悄退回去。
TEST_CASE("bus serves PWA assets with no-store", "[bus][http][web]") {
  BusFixture f;
  for (const auto* target : {"/", "/app.js", "/sw.js", "/manifest.webmanifest"}) {
    const auto response = httpGet(f.server->port(), QByteArray(target));
    CHECK(response.status == 200);
    CHECK(response.head.toLower().contains("cache-control: no-store"));
  }
}

// ============================================================================
// /api/control —— 唯一能让电台发射的 HTTP 出口（PWA 与 MCP 都走它）。
// 以前一条用例都没有，于是三个真缺陷在里面躺了很久：ptt_off 连按两下会把 RX
// 按成 TX、tune 发两帧设频、ptt_on 把「已经关掉」报成「还等着确认」。
// 断言一律落在物理锚点上：mock 的 sim().ptt / 实际发出的帧数，而不是回包字面量。
// ============================================================================

TEST_CASE("control ptt_on needs two taps and reports the real phase",
          "[bus][control]") {
  CountingBusFixture f;
  REQUIRE(f.session->store().snapshot().freq_hz == 14'270'000);
  REQUIRE_FALSE(f.mock->sim().ptt);

  const auto first = jsonResult(
      httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})"));
  CHECK(first.value("ptt").toString() == "confirm_pending");
  CHECK_FALSE(f.mock->sim().ptt);
  CHECK(f.transport->pttWrites(true) == 0);

  const auto second = jsonResult(
      httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})"));
  CHECK(second.value("ptt").toString() == "tx");
  settle([&] { return f.mock->sim().ptt; });
  CHECK(f.mock->sim().ptt);
  CHECK(f.transport->pttWrites(true) == 1);

  // 第三下是「关」：相位必须说 rx，不能还说 confirm_pending
  const auto third = jsonResult(
      httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})"));
  CHECK(third.value("ptt").toString() == "rx");
  settle([&] { return !f.mock->sim().ptt; });
  CHECK_FALSE(f.mock->sim().ptt);
  CHECK(f.transport->pttWrites(false) == 1);
}

TEST_CASE("state exposes the gate phase apart from the radio direction",
          "[bus][control][http]") {
  CountingBusFixture f;
  const auto state = [&] {
    return jsonBody(httpGet(f.server->port(), "/api/state"));
  };
  CHECK(state().value("ptt_phase").toString() == "rx");

  httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})");
  // 武装但没发射：ptt_on 还是 false，ptt_phase 已经是 confirm_pending。
  // 写工具正是靠这个字段才敢要求「确认必须显式」——否则一次调用就能把电台按上去。
  CHECK(state().value("ptt_phase").toString() == "confirm_pending");
  CHECK_FALSE(state().value("ptt_on").toBool());

  httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})");
  CHECK(state().value("ptt_phase").toString() == "tx");
  // ptt_on 是回读来的（250 ms 轮询），不是「我发了命令所以我说了算」：
  // 以前 pollRadio 根本不发 0x1C 读帧，这个字段对着正在发射的电台永远报 false。
  settle([&] { return f.session->store().snapshot().ptt_on; });
  CHECK(state().value("ptt_on").toBool());

  httpPost(f.server->port(), "/api/control", R"({"action":"ptt_off"})");
  CHECK(state().value("ptt_phase").toString() == "rx");
  settle([&] { return !f.session->store().snapshot().ptt_on; });
  CHECK_FALSE(state().value("ptt_on").toBool());
}

TEST_CASE("control ptt_off from RX never keys the transmitter",
          "[bus][control][safety]") {
  CountingBusFixture f;
  // 旧写法在这里连按两下 pttButtonPressed()：Rx→待确认→Tx，「关发射」把电台按成了发射。
  // 多按几次证明它不是「第一次侥幸没发」，而是这个动作根本不该碰门禁的武装路径。
  for (int i = 0; i < 3; ++i) {
    const auto r = jsonResult(
        httpPost(f.server->port(), "/api/control", R"({"action":"ptt_off"})"));
    CHECK(r.value("ptt").toString() == "rx");
  }
  CHECK_FALSE(f.mock->sim().ptt);
  CHECK(f.transport->pttWrites(true) == 0);
  CHECK(f.transport->pttWrites(false) == 0);
  CHECK(f.session->gate().phase() == radio::core::PttPhase::Rx);
}

TEST_CASE("control ptt_off cancels a pending confirmation without arming TX",
          "[bus][control][safety]") {
  CountingBusFixture f;
  const auto armed = jsonResult(
      httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})"));
  REQUIRE(armed.value("ptt").toString() == "confirm_pending");

  const auto cancelled = jsonResult(
      httpPost(f.server->port(), "/api/control", R"({"action":"ptt_off"})"));
  CHECK(cancelled.value("ptt").toString() == "rx");
  CHECK_FALSE(f.mock->sim().ptt);
  CHECK(f.transport->pttWrites(true) == 0);

  // 取消之后门禁必须回到「未武装」：再按一次仍然是待确认，而不是直接进 TX
  const auto again = jsonResult(
      httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})"));
  CHECK(again.value("ptt").toString() == "confirm_pending");
  CHECK_FALSE(f.mock->sim().ptt);
}

TEST_CASE("control ptt_off from TX lands in RX, not in confirm_pending",
          "[bus][control][safety]") {
  CountingBusFixture f;
  httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})");
  const auto tx = jsonResult(
      httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})"));
  REQUIRE(tx.value("ptt").toString() == "tx");
  settle([&] { return f.mock->sim().ptt; });
  REQUIRE(f.mock->sim().ptt);

  const auto off = jsonResult(
      httpPost(f.server->port(), "/api/control", R"({"action":"ptt_off"})"));
  // 旧写法从 Tx 出发按两下会停在 ConfirmPending：电台关了，门禁却还武装着
  CHECK(off.value("ptt").toString() == "rx");
  settle([&] { return !f.mock->sim().ptt; });
  CHECK_FALSE(f.mock->sim().ptt);
  CHECK(f.session->gate().phase() == radio::core::PttPhase::Rx);
  CHECK(f.transport->pttWrites(true) == 1);
  CHECK(f.transport->pttWrites(false) == 1);
}

TEST_CASE("control tune sends exactly one set-frequency frame",
          "[bus][control]") {
  CountingBusFixture f;
  const auto r = jsonResult(httpPost(f.server->port(), "/api/control",
                                     R"({"action":"tune","freq":7100000})"));
  CHECK(r.value("tuned").toBool());
  CHECK(r.value("target_hz").toDouble() == 7'100'000.0);
  CHECK_FALSE(r.contains("error"));
  // 旧写法调了两次 tuneTo：两帧设频 + 被拒时闪两次提示。mock 状态看不出重发，只能数帧。
  CHECK(f.transport->count(radio::core::civ::kCmdSetFreq) == 1);
  settle([&] { return f.session->store().snapshot().freq_hz == 7'100'000; });
  CHECK(f.session->store().snapshot().freq_hz == 7'100'000);
  CHECK(f.mock->sim().freq_hz == 7'100'000);
}

TEST_CASE("control tune outside the RX range is rejected without touching the radio",
          "[bus][control][safety]") {
  CountingBusFixture f;
  const auto before = f.session->store().snapshot().freq_hz;
  const auto r = jsonResult(httpPost(f.server->port(), "/api/control",
                                     R"({"action":"tune","freq":20000})"));
  CHECK_FALSE(r.value("tuned").toBool());
  CHECK(r.value("error").toString() == "rejected");
  // 被拒时 tuned/target_hz 也得在：旧写法第二次失败会把整个 result 换成 {"error":...}
  CHECK(r.value("target_hz").toDouble() == 20'000.0);
  CHECK(f.transport->count(radio::core::civ::kCmdSetFreq) == 0);
  CHECK(f.session->store().snapshot().freq_hz == before);
  CHECK(f.mock->sim().freq_hz == before);
}

TEST_CASE("control cannot key TX on a receivable but non-amateur frequency",
          "[bus][control][safety]") {
  CountingBusFixture f;
  // 100 MHz 在原厂接收范围内（可听），但不在业余白名单里（不可发）
  const auto tuned = jsonResult(httpPost(f.server->port(), "/api/control",
                                         R"({"action":"tune","freq":100000000})"));
  REQUIRE(tuned.value("tuned").toBool());
  settle([&] { return f.session->store().snapshot().freq_hz == 100'000'000; });
  REQUIRE(f.session->store().snapshot().freq_hz == 100'000'000);

  httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})");
  const auto second = jsonResult(
      httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})"));
  CHECK(second.value("ptt").toString() == "rx");
  CHECK_FALSE(f.mock->sim().ptt);
  CHECK(f.transport->pttWrites(true) == 0);
  CHECK(f.session->gate().phase() == radio::core::PttPhase::Rx);
}

TEST_CASE("control cannot key TX before the tune readback lands", "[bus][control][safety]") {
  CountingBusFixture f;
  // 上一条用例 settle 等回读，恰好绕开了真正危险的窗口：设频帧已发、store 还是
  // 旧频率（默认 14.27 MHz，本身在业余段）。这里不等回读就两击。
  // 窗口本身的确定性证明在 test_session_tx.cpp（那边不跑事件循环）。
  const auto tuned = jsonResult(httpPost(f.server->port(), "/api/control",
                                         R"({"action":"tune","freq":100000000})"));
  REQUIRE(tuned.value("tuned").toBool());

  httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})");
  const auto second = jsonResult(
      httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})"));
  CHECK(second.value("ptt").toString() == "rx");
  CHECK(f.transport->pttWrites(true) == 0);
  CHECK_FALSE(f.mock->sim().ptt);
}

TEST_CASE("control set_mode applies a known mode and refuses an unknown one",
          "[bus][control]") {
  CountingBusFixture f;
  const auto ok = jsonResult(httpPost(f.server->port(), "/api/control",
                                      R"({"action":"set_mode","mode":"CW"})"));
  CHECK(ok.value("mode").toString() == "CW");
  settle([&] {
    return f.session->store().snapshot().mode == radio::core::civ::Mode::CW;
  });
  CHECK(f.session->store().snapshot().mode == radio::core::civ::Mode::CW);
  CHECK(f.mock->sim().mode == radio::core::civ::Mode::CW);

  const auto bad = jsonBody(httpPost(f.server->port(), "/api/control",
                                     R"({"action":"set_mode","mode":"NOPE"})"));
  CHECK(bad.value("error").toString() == "bad mode");
  CHECK_FALSE(bad.value("ok").toBool());
  CHECK(f.mock->sim().mode == radio::core::civ::Mode::CW);
}

TEST_CASE("control refuses unknown actions and malformed JSON",
          "[bus][control]") {
  CountingBusFixture f;
  const auto unknown = jsonBody(
      httpPost(f.server->port(), "/api/control", R"({"action":"self_destruct"})"));
  CHECK(unknown.value("error").toString() == "unknown action");
  const auto broken =
      jsonBody(httpPost(f.server->port(), "/api/control", "this is not json"));
  CHECK(broken.value("error").toString() == "bad json");
  CHECK_FALSE(f.mock->sim().ptt);
  CHECK(f.transport->count(radio::core::civ::kCmdSetFreq) == 0);
}

TEST_CASE("bus write endpoints enforce the token too", "[bus][control][security]") {
  BusFixture f("secret");
  CHECK(httpPost(f.server->port(), "/api/control",
                 R"({"action":"ptt_on"})").status == 401);
  CHECK(httpPost(f.server->port(), "/api/qso",
                 R"({"callsign":"BG1ABC"})").status == 401);
  CHECK(httpPost(f.server->port(), "/api/control", R"({"action":"ptt_on"})",
                 "X-Radio-Token: secret\r\n").status == 200);
  CHECK(httpGet(f.server->port(), "/api/qso.adif").status == 401);
  CHECK(httpGet(f.server->port(), "/api/qso.adif?token=secret").status == 200);
}

TEST_CASE("qso log requires a callsign and exports ADIF records", "[bus][qso]") {
  BusFixture f;
  const auto empty = jsonBody(
      httpPost(f.server->port(), "/api/qso", R"({"callsign":"   "})"));
  CHECK(empty.value("error").toString() == "callsign required");

  const auto nested = jsonBody(httpPost(
      f.server->port(), "/api/qso",
      R"({"qso":{"callsign":"BG1ABC","freq_mhz":"14.270","mode":"USB","rst_sent":"59"}})"));
  CHECK(nested.value("ok").toBool());
  CHECK(nested.value("count").toDouble() == 1.0);

  // 扁平形状也收（PWA 与 MCP 各写各的，别只认一种）
  const auto flat = jsonBody(
      httpPost(f.server->port(), "/api/qso", R"({"callsign":"BH2XYZ"})"));
  CHECK(flat.value("count").toDouble() == 2.0);

  const auto adif = httpGet(f.server->port(), "/api/qso.adif");
  REQUIRE(adif.status == 200);
  CHECK(adif.body.contains("<CALL:6>BG1ABC"));
  CHECK(adif.body.contains("<FREQ:6>14.270"));
  CHECK(adif.body.contains("<MODE:3>USB"));
  CHECK(adif.body.contains("<RST_SENT:2>59"));
  CHECK(adif.body.contains("<CALL:6>BH2XYZ"));
  CHECK(adif.body.count("<EOR>") == 2);
}
