// SPDX-License-Identifier: AGPL-3.0-or-later
// CI-V 串口对端：本机没有 CAT 线，所以这里验的是**失败路径必须诚实**——
// 打不开就说打不开，不装作已连接，网关也不许在电台侧没起来时先监听。
// 真实收发字节（含 CI-V 总线回声）留到真机，别把这几条绿读成「串口通了」。
//
// 用例名一律 ASCII：Windows 上 ctest 把中文用例名按本地代码页转一手再传给 Catch2，
// 结果是「No test cases matched」——直接跑 exe 全绿、走 ctest 全红。中文只写注释。
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "core/transport_serial.h"
#include "core/transport_tcp_gateway.h"
#include "qt_test_app.h"

namespace {

using radio::core::CivTcpGateway;
using radio::core::SerialCivTransport;

// 三个平台上都不存在的设备名：Windows 不是 COMn，Linux/macOS 在 /dev 下也没有
constexpr const char* kNoSuchPort = "RS_SUITE_NO_SUCH_CIV_PORT";

std::vector<uint8_t> readFreqFrame() {
  return {0xFE, 0xFE, 0xA4, 0xE0, 0x03, 0xFD};
}

}  // namespace

TEST_CASE("serial civ: send/poll/close before open are safe no-ops", "[serial]") {
  static_cast<void>(qtTestApp());
  SerialCivTransport t({kNoSuchPort, 19200});
  REQUIRE_FALSE(t.isOpen());
  REQUIRE_FALSE(t.send(readFreqFrame()));
  REQUIRE(t.poll() == 0);
  t.close();
  t.close();  // 幂等：析构路径上还会再 close 一次
  REQUIRE_FALSE(t.isOpen());
  REQUIRE(t.framesDelivered() == 0);
  REQUIRE(t.bytesDiscarded() == 0);
}

TEST_CASE("serial civ: empty device name is refused without touching the system", "[serial]") {
  static_cast<void>(qtTestApp());
  SerialCivTransport t({std::string(), 19200});
  std::string err;
  REQUIRE_FALSE(t.open(&err));
  REQUIRE(err.find("串口名为空") != std::string::npos);
  REQUIRE_FALSE(t.isOpen());
}

TEST_CASE("serial civ: unopenable device reports the truth and stays closed on retry", "[serial]") {
  static_cast<void>(qtTestApp());
  SerialCivTransport t({kNoSuchPort, 19200});
  for (int attempt = 0; attempt < 2; ++attempt) {
    std::string err;
    REQUIRE_FALSE(t.open(&err));
    REQUIRE(err.find(kNoSuchPort) != std::string::npos);
    REQUIRE_FALSE(t.isOpen());
    REQUIRE_FALSE(t.send(readFreqFrame()));
    REQUIRE(t.poll() == 0);
  }
}

TEST_CASE("civ gateway: refuses to listen when the rig-side serial port cannot open", "[serial][gateway]") {
  static_cast<void>(qtTestApp());
  auto rig = std::make_shared<SerialCivTransport>(
      SerialCivTransport::Config{kNoSuchPort, 19200});
  CivTcpGateway::Config cfg;  // 127.0.0.1，port=0 让系统挑
  CivTcpGateway gw(cfg, rig);
  std::string err;
  REQUIRE_FALSE(gw.start(&err));
  REQUIRE(err.find("电台侧打不开") != std::string::npos);
  REQUIRE_FALSE(gw.listening());
  REQUIRE(gw.port() == 0);
  REQUIRE_FALSE(gw.clientConnected());
}