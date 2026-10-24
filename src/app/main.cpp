// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/main.cpp — 入口
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <memory>

#include "app/MainWindow.h"
#include "core/transport_mock.h"
#ifdef RADIO_SUITE_HAVE_UDP
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
  QCommandLineOption radio_opt("radio", "真机链路：电台 IP（RS-BA1 Server Function）", "host");
  QCommandLineOption user_opt("user", "电台侧 RS-BA1 用户名", "user");
  QCommandLineOption pass_opt("pass", "电台侧 RS-BA1 密码", "pass");
  QCommandLineOption headless_opt("headless", "无界面总线模式（M5 实现，当前占位）");
  QCommandLineOption bus_opt({"b", "bus"}, "总线端口（配合 --headless，M5）", "port");
  cli.addOption(mock_opt);
  cli.addOption(radio_opt);
  cli.addOption(user_opt);
  cli.addOption(pass_opt);
  cli.addOption(headless_opt);
  cli.addOption(bus_opt);
  cli.addHelpOption();
  cli.process(app);

  if (cli.isSet(headless_opt) || cli.isSet(bus_opt))
    qWarning() << "--headless/--bus 将在 M5 落地，M0 忽略并进入 GUI";

  std::shared_ptr<radio::core::ICivTransport> transport;
#ifdef RADIO_SUITE_HAVE_UDP
  if (cli.isSet(radio_opt)) {
    radio::core::Rsba1UdpTransport::Config cfg;
    cfg.host = cli.value(radio_opt).toStdString();
    cfg.username = cli.value(user_opt).toStdString();
    cfg.password = cli.value(pass_opt).toStdString();
    transport = std::make_shared<radio::core::Rsba1UdpTransport>(std::move(cfg));
  }
#endif
  if (!transport) transport = std::make_shared<radio::core::MockTransport>();

  MainWindow win(transport);
  win.resize(1280, 720);
  win.show();
  return app.exec();
}
