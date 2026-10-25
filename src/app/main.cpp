// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/main.cpp — 入口
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <memory>

#include "app/MainWindow.h"
#include "app/session.h"
#include "core/transport_mock.h"
#ifdef RADIO_SUITE_HAVE_BUS
#include "bus/bus_server.h"
#endif
#ifdef RADIO_SUITE_HAVE_UDP
#include "core/transport_pybridge.h"
#include "core/transport_udp.h"
#endif

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("radio-suite");
  QApplication::setApplicationVersion("0.1.0");
  QApplication::setOrganizationName("radio-suite");

  QCommandLineParser cli;
  cli.setApplicationDescription(
      "radio-suite M0 spike — 独立业余电台控制台（AGPL-3.0）");
  QCommandLineOption mock_opt("mock", "使用内置 mock 电台");
  QCommandLineOption radio_opt("radio", "真机链路（主线=C++ 原生 UDP 转写）：电台 IP", "host");
  QCommandLineOption radio_py_opt("radio-py", "后手兜底：改走 rsba1-core Python 桥（真机验证栈）");
  QCommandLineOption user_opt("user", "电台侧 RS-BA1 用户名", "user");
  QCommandLineOption pass_opt("pass", "电台侧 RS-BA1 密码", "pass");
  QCommandLineOption python_opt("python", "Python 解释器（默认 PATH 上的 python）", "exe");
  QCommandLineOption headless_opt("headless", "无界面总线模式（M5 实现，当前占位）");
  QCommandLineOption bus_opt({"b", "bus"}, "总线端口（配合 --headless，M5）", "port");
  cli.addOption(mock_opt);
  cli.addOption(radio_opt);
  cli.addOption(radio_py_opt);
  cli.addOption(user_opt);
  cli.addOption(pass_opt);
  cli.addOption(python_opt);
  cli.addOption(headless_opt);
  cli.addOption(bus_opt);
  cli.addHelpOption();
  cli.process(app);

  if (cli.isSet(headless_opt) || cli.isSet(bus_opt)) {
    // ---- headless 总线模式（scratchpad/lumo 集成，M5 第一片）----
#ifdef RADIO_SUITE_HAVE_BUS
    uint16_t port = static_cast<uint16_t>(cli.value(bus_opt).toInt());
    if (port == 0) port = 8765;
    std::shared_ptr<radio::core::ICivTransport> transport =
        std::make_shared<radio::core::MockTransport>();
#ifdef RADIO_SUITE_HAVE_UDP
    if (cli.isSet(radio_opt)) {
      radio::core::Rsba1UdpTransport::Config cfg;
      cfg.host = cli.value(radio_opt).toStdString();
      cfg.username = cli.value(user_opt).toStdString();
      cfg.password = cli.value(pass_opt).toStdString();
      transport = std::make_shared<radio::core::Rsba1UdpTransport>(std::move(cfg));
    }
#endif
    radio::app::RadioSession session(transport);
    session.start();
    radio::bus::BusServer bus(session);
    radio::bus::BusServer::Config bcfg;
    bcfg.port = port;
    QString err;
    if (!bus.start(bcfg, &err)) {
      qCritical() << err;
      return 1;
    }
    qInfo() << "headless 总线模式运行中，Ctrl+C 退出";
    return app.exec();
#else
    qCritical() << "本构建未含 HTTP 总线（缺 Qt6::HttpServer）";
    return 1;
#endif
  }

  std::shared_ptr<radio::core::ICivTransport> transport;
#ifdef RADIO_SUITE_HAVE_UDP
  if (cli.isSet(radio_opt)) {
    const bool use_bridge = cli.isSet("radio-py");
    if (use_bridge) {
      // rsba1-core（Python，MIT，真机验证 2026-08-18）桥：tools/rsba1_bridge.py
      radio::core::PyBridgeTransport::Config cfg;
      cfg.host = cli.value(radio_opt).toStdString();
      cfg.username = cli.value(user_opt).toStdString();
      cfg.password = cli.value(pass_opt).toStdString();
      if (cli.isSet(python_opt))
        cfg.python_exe = cli.value(python_opt).toStdString();
      transport = std::make_shared<radio::core::PyBridgeTransport>(std::move(cfg));
      qInfo() << "真机链路：rsba1-core Python 桥（后手兜底）";
    } else {
      radio::core::Rsba1UdpTransport::Config cfg;
      cfg.host = cli.value(radio_opt).toStdString();
      cfg.username = cli.value(user_opt).toStdString();
      cfg.password = cli.value(pass_opt).toStdString();
      transport = std::make_shared<radio::core::Rsba1UdpTransport>(std::move(cfg));
      qInfo() << "真机链路：C++ 原生 UDP";
    }
  }
#endif
  if (!transport) transport = std::make_shared<radio::core::MockTransport>();

  MainWindow win(transport);
  win.resize(1280, 720);
  win.show();
  return app.exec();
}
