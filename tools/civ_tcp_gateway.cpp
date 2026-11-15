// SPDX-License-Identifier: AGPL-3.0-or-later
// 台架小工具：CI-V over TCP 网关（PC 侧）。
//
// 为什么需要它：手机端没有 QtSerialPort，也拿不到电台的 USB CAT 口；但手机能连 TCP。
// 这个工具在 PC 上把「电台那一侧」和「一条 TCP 连接」接起来，手机 radio-suite 选
// 「CI-V over TCP」就能像插了根串口线一样控制电台。
//
// 电台侧两种：默认 --mock（内置模拟电台），或 --serial <设备名> 接真电台的 CAT 口。
// 本构建没编进 Qt6::SerialPort 时 --serial 会当面报错退出，**不会静默退回 mock**——
// 那会让人以为在控制电台，其实在跟一个模拟器说话。
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#include <iostream>
#include <memory>
#include <string>

#include "core/transport_mock.h"
#include "core/transport_tcp_gateway.h"

#ifdef RADIO_SUITE_HAVE_SERIAL
#include "core/transport_serial.h"
#endif

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
  QCommandLineOption serial_opt(QStringList() << "s" << "serial",
                                "电台侧接真串口（COM3 / /dev/ttyUSB0），不给则用 mock",
                                "device");
  QCommandLineOption baud_opt(QStringList() << "baud",
                              "串口波特率，必须与电台 CI-V 菜单一致", "baud", "19200");
  parser.addOption(port_opt);
  parser.addOption(bind_opt);
  parser.addOption(serial_opt);
  parser.addOption(baud_opt);
  parser.process(app);

  radio::core::CivTcpGateway::Config cfg;
  cfg.bind_addr = parser.value(bind_opt).toStdString();
  cfg.port = static_cast<uint16_t>(parser.value(port_opt).toUInt());

  std::shared_ptr<radio::core::ICivTransport> rig;
  std::string rig_desc;
  if (parser.isSet(serial_opt)) {
#ifdef RADIO_SUITE_HAVE_SERIAL
    radio::core::SerialCivTransport::Config scfg;
    scfg.port = parser.value(serial_opt).toStdString();
    scfg.baud = parser.value(baud_opt).toInt();
    rig_desc = "串口 " + scfg.port + " @" + std::to_string(scfg.baud) + " 8N1";
    rig = std::make_shared<radio::core::SerialCivTransport>(std::move(scfg));
#else
    std::cerr << "本构建没编进 Qt6::SerialPort，--serial 用不了。"
              << "装上 Qt6::SerialPort 重新 configure，或去掉 --serial 用 mock。"
              << std::endl;
    return 1;
#endif
  } else {
    rig = std::make_shared<radio::core::MockTransport>();
    rig_desc = "mock IC-705（14.270 MHz USB）";
  }
  radio::core::CivTcpGateway gw(cfg, rig);

  std::string err;
  if (!gw.start(&err)) {
    std::cerr << "网关起不来：" << err << std::endl;
    return 1;
  }
  std::cout << "listening " << cfg.bind_addr << ":" << gw.port()
            << " · 电台侧 = " << rig_desc << std::endl;
  std::cout << "手机：连接… → 链路选「CI-V over TCP」→ 网关 IP 填这台机器的地址"
            << std::endl;

  QTimer pump;
  // 20 ms：手机侧 session 是 250 ms 轮询，网关再慢就把延迟叠上去了
  QObject::connect(&pump, &QTimer::timeout, [&gw] { gw.pump(); });
  pump.start(20);
  return app.exec();
}
