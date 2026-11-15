// SPDX-License-Identifier: AGPL-3.0-or-later
// 台架小工具：CI-V over TCP 网关（PC 侧）。
//
// 为什么需要它：手机端没有 QtSerialPort，也拿不到电台的 USB CAT 口；但手机能连 TCP。
// 这个工具在 PC 上把「电台那一侧」和「一条 TCP 连接」接起来，手机 radio-suite 选
// 「CI-V over TCP」就能像插了根串口线一样控制电台。
//
// 电台侧两种来源：--mock（内置模拟电台）或 --serial COM3（真电台 CAT 口）。
// 后者靠 core/transport_serial 的 SerialCivTransport；套件没有 Qt6::SerialPort
// 时这个工具仍能构建，但 --serial 会如实报错，不假装支持。
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#include <iostream>
#include <memory>
#include <string>

#include "core/transport_mock.h"
#include "core/transport_serial.h"
#include "core/transport_tcp_gateway.h"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName("civ_tcp_gateway");
  QCoreApplication::setApplicationVersion("0.1");

  QCommandLineParser parser;
  parser.setApplicationDescription(
      "CI-V over TCP 网关：一条 TCP 连接 = 一根串口线（电台侧 = mock 或真串口）");
  parser.addHelpOption();
  QCommandLineOption port_opt(QStringList() << "p" << "port",
                              "监听端口（0 = 让系统挑）", "port", "4533");
  QCommandLineOption bind_opt(QStringList() << "b" << "bind",
                              "监听地址（局域网给手机连时填 0.0.0.0）", "bind",
                              "127.0.0.1");
  QCommandLineOption serial_opt({"s", "serial"},
                                "电台侧用真串口（COM3 或 /dev/ttyUSB0），不给就是 mock",
                                "port");
  QCommandLineOption baud_opt({"B", "baud"}, "串口波特率（IC-705 出厂 CAT 是 9600）",
                              "baud", "9600");
  parser.addOption(port_opt);
  parser.addOption(bind_opt);
  parser.addOption(serial_opt);
  parser.addOption(baud_opt);
  parser.process(app);

  radio::core::CivTcpGateway::Config cfg;
  cfg.bind_addr = parser.value(bind_opt).toStdString();
  cfg.port = static_cast<uint16_t>(parser.value(port_opt).toUInt());

  std::shared_ptr<radio::core::ICivTransport> rig;
  const QString rig_desc = [&] {
    if (!parser.isSet(serial_opt)) {
      rig = std::make_shared<radio::core::MockTransport>();
      return QStringLiteral("mock IC-705（14.270 MHz USB）");
    }
    radio::core::SerialCivTransport::Config scfg;
    scfg.port_name = parser.value(serial_opt).toStdString();
    scfg.baud = parser.value(baud_opt).toInt();
    // 串口在网关启动时就打开：晚到第一个客户端连上才开，会把「口不存在」
    // 这种硬失败拖成手机侧的莫名超时
    auto serial = std::make_shared<radio::core::SerialCivTransport>(scfg);
    std::string serr;
    if (!serial->open(&serr)) {
      std::cerr << "电台侧串口起不来：" << serr << std::endl;
      return QString();
    }
    rig = serial;
    return QString("CI-V over %1 @%2 8N1")
        .arg(QString::fromStdString(scfg.port_name))
        .arg(scfg.baud);
  }();
  if (rig_desc.isEmpty()) return 1;

  radio::core::CivTcpGateway gw(cfg, rig);

  std::string err;
  if (!gw.start(&err)) {
    std::cerr << "网关起不来：" << err << std::endl;
    return 1;
  }
  std::cout << "listening " << cfg.bind_addr << ":" << gw.port()
            << " · 电台侧 = " << rig_desc.toStdString() << std::endl;
  std::cout << "手机：连接… → 链路选「CI-V over TCP」→ 网关 IP 填这台机器的地址"
            << std::endl;

  QTimer pump;
  // 20 ms：手机侧 session 是 250 ms 轮询，网关再慢就把延迟叠上去了
  QObject::connect(&pump, &QTimer::timeout, [&gw] { gw.pump(); });
  pump.start(20);
  return app.exec();
}
