// SPDX-License-Identifier: AGPL-3.0-or-later
// /api/state 的链路活性字段：总线上那些频率/模式/S 表数是「最后一次收到回帧时的
// 快照」，断链后既不变也不归零。没有活性判据，客户端就会把十分钟前的读数当成
// 此刻的电台状态（G-6）。这里验的就是那个判据不许说谎：
//   1) 从来没通过 → -1，不是 0（「从未」与「刚断」必须分得开）
//   2) 真在回话   → alive，age 是个非负数
//   3) 不说话了   → 判死，age 越过阈值，但旧读数保留；再回话要能活过来
//
// 断链靠「真的不回话」造，不是改 store 里的数——后者验的是 JSON 拼装，不是活性判定。
//
// 用例名一律 ASCII：Windows 上 ctest 把中文用例名按本地代码页转一手再传给 Catch2，
// 结果是「No test cases matched」——直接跑 exe 全绿、走 ctest 全红。中文只写注释。
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QThread>
#include <functional>
#include <memory>
#include <span>

#include "bus/bus_server.h"
#include "core/transport_mock.h"
#include "qt_test_app.h"

namespace {

void pump() {
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  QThread::msleep(1);
}

bool waitUntil(const std::function<bool()>& pred, int timeout_ms = 4000) {
  QElapsedTimer timer;
  timer.start();
  while (!pred() && timer.elapsed() < timeout_ms) pump();
  return pred();
}

void pumpFor(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) pump();
}

// 能闭嘴的假电台：包一层 MockTransport，mute 时把应答吞掉不上抛。
// MockTransport 是 final，只能组合不能继承；会话的回调装在外层，
// 内层的回调经这里转发，所以「吞掉」发生在会话看到帧之前。
class MuteableRadio final : public radio::core::ICivTransport {
 public:
  MuteableRadio() {
    inner_.setFrameCallback([this](std::span<const uint8_t> f) {
      if (!muted_) emitFrame(f);
    });
  }

  bool open(std::string* error) override { return inner_.open(error); }
  void close() override { inner_.close(); }
  bool isOpen() const override { return inner_.isOpen(); }
  bool send(std::span<const uint8_t> frame) override { return inner_.send(frame); }
  size_t poll() override {
    const size_t n = inner_.poll();
    return muted_ ? 0 : n;
  }

  void mute() { muted_ = true; }
  void unmute() { muted_ = false; }

 private:
  radio::core::MockTransport inner_;
  bool muted_ = false;
};

struct LinkFixture {
  // muted 必须在 start() 之前生效：start() 里就 pollRadio() 一次，
  // 晚一步闭嘴的话「从来没通过」已经被那一轮回帧破坏了。
  explicit LinkFixture(bool muted = false) {
    qtTestApp();
    radio = std::make_shared<MuteableRadio>();
    if (muted) radio->mute();
    session = std::make_unique<radio::app::RadioSession>(radio);
    session->start();
    server = std::make_unique<radio::bus::BusServer>(*session);
    radio::bus::BusServer::Config cfg;
    cfg.port = 0;
    QString error;
    REQUIRE(server->start(cfg, &error));
  }

  std::shared_ptr<MuteableRadio> radio;
  std::unique_ptr<radio::app::RadioSession> session;
  std::unique_ptr<radio::bus::BusServer> server;
};

// 按 Content-Length 收满就走，不等对端关连接：QHttpServer 不理会 Connection: close，
// 等 UnconnectedState 会每次白等满超时（实测每条用例多花 2 s）。
QJsonObject getState(uint16_t port) {
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, port);
  QElapsedTimer timer;
  timer.start();
  while (socket.state() != QAbstractSocket::ConnectedState && timer.elapsed() < 2000)
    pump();
  REQUIRE(socket.state() == QAbstractSocket::ConnectedState);
  const QByteArray req =
      "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
  REQUIRE(socket.write(req) == req.size());

  QByteArray raw;
  int split = -1;
  qint64 want = -1;
  timer.restart();
  while (timer.elapsed() < 3000) {
    pump();
    raw += socket.readAll();
    if (split < 0) {
      split = raw.indexOf("\r\n\r\n");
      if (split >= 0) {
        const QByteArray head = raw.left(split);
        REQUIRE(head.left(head.indexOf("\r\n")).split(' ').value(1).toInt() == 200);
        for (const QByteArray& line : head.split('\n')) {
          if (line.toLower().startsWith("content-length:"))
            want = line.mid(15).trimmed().toLongLong();
        }
      }
    }
    if (split >= 0 && want >= 0 && raw.size() - split - 4 >= want) break;
  }
  REQUIRE(split >= 0);
  REQUIRE(want >= 0);
  REQUIRE(raw.size() - split - 4 >= want);
  const auto doc = QJsonDocument::fromJson(raw.mid(split + 4, static_cast<int>(want)));
  REQUIRE(doc.isObject());
  return doc.object();
}

}  // namespace

TEST_CASE("bus state calls a never-answered link never, not fresh", "[bus][link]") {
  LinkFixture f(true);
  f.session->setLinkTimeoutMs(1234);
  pumpFor(300);  // 让 100 ms 的 tick 跑几轮

  const auto j = getState(f.server->port());
  CHECK_FALSE(j.value("link_alive").toBool());
  // -1 是「至今一个能解析的回帧都没给过」。写成 0 会被客户端渲染成「刚刚才通」。
  CHECK(j.value("link_reply_age_ms").toInteger() == -1);
  // 阈值必须跟着会话走，不能在总线里写死 2000
  CHECK(j.value("link_timeout_ms").toInteger() == 1234);
  CHECK_FALSE(f.session->linkAlive());
  CHECK(f.session->replyAgeMs() == -1);
}

TEST_CASE("bus state marks the link alive while the radio keeps answering",
          "[bus][link]") {
  LinkFixture f;
  REQUIRE(waitUntil([&] { return f.session->linkAlive(); }));

  const auto j = getState(f.server->port());
  CHECK(j.value("link_alive").toBool());
  const auto age = j.value("link_reply_age_ms").toInteger();
  CHECK(age >= 0);
  CHECK(age <= j.value("link_timeout_ms").toInteger());
  CHECK(j.value("link_timeout_ms").toInteger() == 2000);
}

TEST_CASE("bus state reports silence as dead but keeps the last known readings",
          "[bus][link]") {
  LinkFixture f;
  REQUIRE(f.session->tuneTo(7'100'000));
  REQUIRE(waitUntil([&] {
    return f.session->store().snapshot().freq_hz == 7'100'000;
  }));
  REQUIRE(waitUntil([&] { return f.session->linkAlive(); }));

  f.radio->mute();
  // 默认阈值 2000 ms + 100 ms 的 tick：判死要等得起，别把超时写成永远不来的等待
  REQUIRE(waitUntil([&] { return !f.session->linkAlive(); }, 6000));

  const auto j = getState(f.server->port());
  CHECK_FALSE(j.value("link_alive").toBool());
  CHECK(j.value("link_reply_age_ms").toInteger() >
        j.value("link_timeout_ms").toInteger());
  // 断链不是把状态抹掉：旧读数留着，靠 link_alive 告诉客户端它已经不可信
  CHECK(j.value("freq_hz").toInteger() == 7'100'000);

  // 再回话必须能活过来：活性不是一次判死就永远锁死
  f.radio->unmute();
  REQUIRE(waitUntil([&] { return f.session->linkAlive(); }));
  const auto back = getState(f.server->port());
  CHECK(back.value("link_alive").toBool());
  CHECK(back.value("link_reply_age_ms").toInteger() >= 0);
}