// SPDX-License-Identifier: AGPL-3.0-or-later
// 端到端：手机侧那套（TcpCivTransport + RadioSession）经 PC 网关驱动 mock 电台。
// 这是 A 线在「没有电台、没有网线」的台架上能拿到的最强证据：
// UI 命令 → TCP → 网关 → 电台 → 回包 → TCP → 会话状态，整条闭环。
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTcpSocket>
#include <QThread>
#include <functional>
#include <memory>

#include "session/session.h"
#include "core/transport_mock.h"
#include "core/transport_serial.h"
#include "core/transport_tcp.h"
#include "core/transport_tcp_gateway.h"
#include "fake_serial_pipe.h"
#include "qt_test_app.h"

namespace {

using radio::core::CivTcpGateway;
using radio::core::MockTransport;
using radio::core::SerialCivTransport;
using radio::core::TcpCivTransport;
using radio::test::FakeSerialPipe;

// 网关是纯被驱动的：没人 pump 它就只是一条没接通的线。
bool pumpUntil(CivTcpGateway& gw, const std::function<bool()>& pred,
               int timeout_ms = 4000) {
  QElapsedTimer timer;
  timer.start();
  while (!pred() && timer.elapsed() < timeout_ms) {
    gw.pump();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(2);
  }
  return pred();
}

struct Harness {
  Harness() {
    qtTestApp();
    rig = std::make_shared<MockTransport>();
    CivTcpGateway::Config gcfg;
    gcfg.bind_addr = "127.0.0.1";
    gcfg.port = 0;  // 让系统挑端口，别和台架上真在跑的网关撞
    gw = std::make_unique<CivTcpGateway>(gcfg, rig);
    std::string err;
    REQUIRE(gw->start(&err));
    REQUIRE(gw->listening());

    TcpCivTransport::Config ccfg;
    ccfg.host = "127.0.0.1";
    ccfg.port = gw->port();
    ccfg.connect_timeout_ms = 2000;
    client = std::make_shared<TcpCivTransport>(std::move(ccfg));
    session = std::make_unique<radio::app::RadioSession>(client);
    session->start();
    REQUIRE(client->isOpen());
  }

  std::shared_ptr<MockTransport> rig;
  std::unique_ptr<CivTcpGateway> gw;
  std::shared_ptr<TcpCivTransport> client;
  std::unique_ptr<radio::app::RadioSession> session;
};

}  // namespace

TEST_CASE("tcp gateway: a session drives the mock rig through the bridge",
          "[gateway][tcp]") {
  Harness h;
  // 会话每 250 ms 撒回读；把这些字节搬过网关，状态就该从电台侧回来
  REQUIRE(pumpUntil(*h.gw, [&] {
    return h.session->store().snapshot().freq_hz == 14'270'000;
  }));
  CHECK(h.gw->clientConnected());
  // 有回帧 → 活性为真。活性翻转是 100 ms tick 驱动的，第一帧回来那一刻还没翻，
  // 所以要等到翻为止（这条同时证明 #32 那套判定在真链路上方向正确）
  CHECK(pumpUntil(*h.gw, [&] { return h.session->linkAlive(); }, 1500));
  CHECK(h.session->replyAgeMs() >= 0);

  // 设频走完整闭环：命令 → TCP → 网关 → 电台 → 回读 → 会话状态
  REQUIRE(h.session->tuneTo(14'074'000));
  CHECK(pumpUntil(*h.gw, [&] {
    return h.session->store().snapshot().freq_hz == 14'074'000;
  }));
}

TEST_CASE("tcp gateway: coalesced writes still reach the rig frame by frame",
          "[gateway][tcp]") {
  Harness h;
  // session 一轮回读是 ~28 帧，TCP 必然粘包。网关若按「一次读 = 一次 send」转发，
  // 电台侧只认得第一帧，后面的状态就全回不来——所以这里断言的是「靠后的那些字段
  // 也回来了」，不是「第一个字段回来了」。
  REQUIRE(pumpUntil(*h.gw, [&] {
    const auto s = h.session->store().snapshot();
    return s.freq_hz == 14'270'000 && s.dtcs_code == 23 &&
           s.offset_hz == 600'000;
  }));
  const auto s = h.session->store().snapshot();
  CHECK(s.smeter_raw == 42);          // 中段的一帧
  CHECK(s.tone_decihz == 885);        // 更靠后的一帧
}

TEST_CASE("tcp gateway: one line at a time, a second client is refused",
          "[gateway][tcp]") {
  Harness h;
  REQUIRE(pumpUntil(*h.gw, [&] { return h.gw->clientConnected(); }));

  QTcpSocket second;
  second.connectToHost(QHostAddress::LocalHost, h.gw->port());
  REQUIRE(second.waitForConnected(2000));
  pumpUntil(*h.gw, [] { return false; }, 250);  // 给网关一轮处理 pending 的机会

  CHECK(std::string(h.gw->log()).find("reject second client") != std::string::npos);
  CHECK(h.gw->clientConnected());  // 第一个客户端没被顶掉
}

TEST_CASE("tcp gateway: hangup frees the line for the next client",
          "[gateway][tcp]") {
  Harness h;
  REQUIRE(pumpUntil(*h.gw, [&] { return h.gw->clientConnected(); }));

  h.client->close();
  REQUIRE(pumpUntil(*h.gw, [&] { return !h.gw->clientConnected(); }));
  CHECK(std::string(h.gw->log()).find("client gone") != std::string::npos);

  TcpCivTransport::Config ccfg;
  ccfg.host = "127.0.0.1";
  ccfg.port = h.gw->port();
  ccfg.connect_timeout_ms = 2000;
  TcpCivTransport again(std::move(ccfg));
  std::string err;
  REQUIRE(again.open(&err));
  CHECK(pumpUntil(*h.gw, [&] { return h.gw->clientConnected(); }));
  again.close();
}

// A3 的收口证据：把网关的电台侧从 mock 换成「SerialCivTransport + 一条虚拟串口线」，
// 手机那套（session + TCP）一个字都不用改就能驱动挂在串口上的电台。
// 台架没有 CAT 线，所以线的那头接的是 MockTransport——但流经的代码是生产那套：
// TcpCivTransport → CivTcpGateway → SerialCivTransport → ICivBytePipe → 电台。
TEST_CASE("tcp gateway: a serial-backed rig drives the phone session",
          "[gateway][serial]") {
  qtTestApp();

  auto rig = std::make_shared<MockTransport>();
  REQUIRE(rig->open(nullptr));
  auto pipe = std::make_unique<FakeSerialPipe>();
  FakeSerialPipe* wire = pipe.get();
  // 电台的应答沿这条虚拟线回到串口传输层的接收缓冲
  rig->setFrameCallback([wire](std::span<const uint8_t> f) { wire->feed(f); });
  wire->responder = [rig_ptr = rig.get()](std::span<const uint8_t> frame) {
    rig_ptr->send(frame);
    rig_ptr->poll();
  };

  SerialCivTransport::Config scfg;
  scfg.port_name = "COM-FAKE";
  scfg.baud = 9600;
  auto serial = std::make_shared<SerialCivTransport>(std::move(scfg), std::move(pipe));
  std::string serr;
  REQUIRE(serial->open(&serr));

  CivTcpGateway::Config gcfg;
  gcfg.bind_addr = "127.0.0.1";
  gcfg.port = 0;
  CivTcpGateway gw(gcfg, serial);
  std::string gerr;
  REQUIRE(gw.start(&gerr));

  TcpCivTransport::Config ccfg;
  ccfg.host = "127.0.0.1";
  ccfg.port = gw.port();
  ccfg.connect_timeout_ms = 2000;
  auto client = std::make_shared<TcpCivTransport>(std::move(ccfg));
  radio::app::RadioSession session(client);
  session.start();
  REQUIRE(client->isOpen());

  // 回读要穿过五层才回来，任何一层漏了都会卡在这里而不是「碰巧过了」
  REQUIRE(pumpUntil(gw, [&] {
    return session.store().snapshot().freq_hz == 14'270'000;
  }));
  CHECK(gw.clientConnected());
  CHECK(session.replyAgeMs() >= 0);

  // 设频：命令 → TCP → 网关 → 串口 → 电台，ACK 原路回来，状态落到会话
  REQUIRE(session.tuneTo(7'074'000));
  CHECK(pumpUntil(gw, [&] {
    return session.store().snapshot().freq_hz == 7'074'000;
  }));
  // 电台侧确实收到了设频请求（不是会话自己把状态改成的）
  CHECK(serial->framesDelivered() > 0);
}
