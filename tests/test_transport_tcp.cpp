// SPDX-License-Identifier: AGPL-3.0-or-later
// CI-V over TCP：字节管道必须自己讲清四件事——连得上/断得开、发出去的字节一字不差、
// 收进来的流按 FD 切帧（粘包与半包都要对）、流错位时丢掉重同步而不是喂假帧。
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "core/transport_tcp.h"
#include "qt_test_app.h"

namespace {

using radio::core::TcpCivTransport;

// 一条合法的最小 CI-V 应答帧（FE FE to from cmd FD）
std::vector<uint8_t> frame(uint8_t to, uint8_t from, uint8_t cmd) {
  return {0xFE, 0xFE, to, from, cmd, 0xFD};
}

bool waitUntil(const std::function<bool()>& pred, int timeout_ms = 2000) {
  QElapsedTimer timer;
  timer.start();
  while (!pred() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(2);
  }
  return pred();
}

// poll() 是「此刻有多少读多少」，网络包什么时候到是不确定的：
// 想验「切出 N 帧」就得反复 poll 到达数，不能指望一次 poll 全中。
size_t pollUntilFrames(TcpCivTransport& t, size_t want, int timeout_ms = 2000) {
  size_t n = 0;
  waitUntil([&] {
    n += t.poll();
    return n >= want;
  }, timeout_ms);
  return n;
}

// 假网关：一条 TCP 连接 = 一根串口线。测试自己决定回什么字节。
class FakeGateway {
 public:
  FakeGateway() { REQUIRE(server_.listen(QHostAddress::LocalHost, 0)); }

  quint16 port() const { return server_.serverPort(); }

  bool accept(int timeout_ms = 2000) {
    if (!waitUntil([&] { return server_.hasPendingConnections(); }, timeout_ms))
      return false;
    peer_.reset(server_.nextPendingConnection());
    return peer_ != nullptr;
  }

  QByteArray readAll(qint64 expect) {
    QByteArray out;
    waitUntil([&] {
      if (peer_) out.append(peer_->readAll());
      return out.size() >= expect;
    });
    if (peer_) out.append(peer_->readAll());
    return out;
  }

  void write(const std::vector<uint8_t>& bytes) {
    if (!peer_) return;
    peer_->write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<qint64>(bytes.size()));
    peer_->flush();
  }

  void dropPeer() {
    if (!peer_) return;
    peer_->disconnectFromHost();
    peer_->waitForDisconnected(500);
    peer_.reset();
  }

 private:
  QTcpServer server_;
  std::unique_ptr<QTcpSocket> peer_;
};

radio::core::TcpCivTransport::Config gwConfig(FakeGateway& gw) {
  radio::core::TcpCivTransport::Config cfg;
  cfg.host = "127.0.0.1";
  cfg.port = gw.port();
  cfg.connect_timeout_ms = 2000;
  return cfg;
}

// 帧形状：FE FE to from cmd FD —— cmd 在索引 4，末尾是 FD 不是 cmd
bool wellFormed(const QByteArray& f) {
  return f.size() >= 6 && static_cast<uint8_t>(f.at(0)) == 0xFE &&
         static_cast<uint8_t>(f.at(1)) == 0xFE &&
         static_cast<uint8_t>(f.at(f.size() - 1)) == 0xFD;
}
int cmdOf(const QByteArray& f) { return static_cast<uint8_t>(f.at(4)); }

}  // namespace

TEST_CASE("tcp transport: open connects and a peer hangup is reported honestly",
          "[transport][tcp]") {
  qtTestApp();
  FakeGateway gw;
  TcpCivTransport t(gwConfig(gw));
  std::string err;
  REQUIRE(t.open(&err));
  CHECK(t.isOpen());
  CHECK(err.empty());
  REQUIRE(gw.accept());

  gw.dropPeer();
  // 对端断开必须体现在 isOpen() 上：链路状态谎报正是清单 G-6 判的那件事
  waitUntil([&] { return !t.isOpen(); });
  t.poll();
  CHECK_FALSE(t.isOpen());
}

TEST_CASE("tcp transport: open against a dead port fails with a reason",
          "[transport][tcp]") {
  qtTestApp();
  // 只 listen 再放手不算「死端口」：macOS runner 上别的进程可能在我们放开的
  // 那一瞬把这个临时端口占走，于是 open() 真连上了、用例红得莫名其妙
  //（CI run 42365446469 的 macos 腿就是这么挂的）。改成每次放手后自己探一次，
  // 连得上就换一个，探不到死端口才承认是环境不给力。
  quint16 dead = 0;
  for (int attempt = 0; attempt < 8 && dead == 0; ++attempt) {
    quint16 port = 0;
    {
      QTcpServer probe;
      REQUIRE(probe.listen(QHostAddress::LocalHost, 0));
      port = probe.serverPort();
    }  // 出了作用域就没人监听了
    QTcpSocket peek;
    peek.connectToHost("127.0.0.1", port);
    if (!peek.waitForConnected(250)) dead = port;
    peek.abort();
  }
  if (dead == 0)
    SKIP("8 个临时端口放手后都被别的进程占着，这台机器给不出可确认的死端口");
  radio::core::TcpCivTransport::Config cfg;
  cfg.host = "127.0.0.1";
  cfg.port = dead;
  cfg.connect_timeout_ms = 800;
  TcpCivTransport t(std::move(cfg));
  std::string err;
  CHECK_FALSE(t.open(&err));
  CHECK_FALSE(t.isOpen());
  CHECK_FALSE(err.empty());
}

TEST_CASE("tcp transport: sent frames reach the gateway byte for byte",
          "[transport][tcp]") {
  qtTestApp();
  FakeGateway gw;
  TcpCivTransport t(gwConfig(gw));
  std::string err;
  REQUIRE(t.open(&err));
  REQUIRE(gw.accept());

  const auto f = frame(0xE0, 0x00, 0x03);
  CHECK(t.send(std::span<const uint8_t>(f.data(), f.size())));
  CHECK(gw.readAll(static_cast<qint64>(f.size())) ==
        QByteArray(reinterpret_cast<const char*>(f.data()),
                   static_cast<int>(f.size())));

  // 断了之后不许假装发成功
  gw.dropPeer();
  waitUntil([&] { return !t.isOpen(); });
  CHECK_FALSE(t.send(std::span<const uint8_t>(f.data(), f.size())));
}

TEST_CASE("tcp transport: inbound stream is split on the frame terminator",
          "[transport][tcp]") {
  qtTestApp();
  FakeGateway gw;
  TcpCivTransport t(gwConfig(gw));
  std::string err;
  REQUIRE(t.open(&err));
  REQUIRE(gw.accept());

  std::vector<QByteArray> got;
  t.setFrameCallback([&got](std::span<const uint8_t> f) {
    got.emplace_back(reinterpret_cast<const char*>(f.data()),
                     static_cast<int>(f.size()));
  });

  // 1) 粘包：两帧一次写
  auto two = frame(0x00, 0xE0, 0x01);
  const auto second = frame(0x00, 0xE0, 0x02);
  two.insert(two.end(), second.begin(), second.end());
  gw.write(two);
  CHECK(pollUntilFrames(t, 2) == 2);
  REQUIRE(got.size() == 2);
  CHECK(wellFormed(got[0]));
  CHECK(wellFormed(got[1]));
  CHECK(cmdOf(got[0]) == 0x01);
  CHECK(cmdOf(got[1]) == 0x02);

  // 2) 半包：一帧分两次写，没收全不许上抛
  const auto third = frame(0x00, 0xE0, 0x03);
  gw.write({third.begin(), third.begin() + 3});
  CHECK(pollUntilFrames(t, 0, 120) == 0);
  gw.write({third.begin() + 3, third.end()});
  CHECK(pollUntilFrames(t, 1) == 1);
  REQUIRE(got.size() == 3);
  CHECK(wellFormed(got[2]));
  CHECK(cmdOf(got[2]) == 0x03);

  // 3) 帧前的垃圾丢掉；短于 6 字节的碎片不许当帧喂给解析器
  std::vector<uint8_t> junk = {'G', 'A', 'B', 'B', 'A', 'G', 'E'};
  const std::vector<uint8_t> tiny = {0xFE, 0xFE, 0xFD};
  const auto good = frame(0x00, 0xE0, 0x04);
  auto messy = junk;
  messy.insert(messy.end(), tiny.begin(), tiny.end());
  messy.insert(messy.end(), good.begin(), good.end());
  gw.write(messy);
  CHECK(pollUntilFrames(t, 1) == 1);
  CHECK(got.size() == 4);
  CHECK(wellFormed(got.back()));
  CHECK(cmdOf(got.back()) == 0x04);
  CHECK(t.framesDelivered() == 4);
  CHECK(t.bytesDiscarded() >= 10);  // 7 个垃圾字节 + 3 字节的假帧
}
