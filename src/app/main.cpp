// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/main.cpp — 入口
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <memory>

#include "app/MainWindow.h"
#include "core/transport_mock.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("radio-suite");
  QApplication::setApplicationVersion("0.1.0");
  QApplication::setOrganizationName("radio-suite");

  QCommandLineParser cli;
  cli.setApplicationDescription(
      "radio-suite M0 spike — 独立业余电台控制台（AGPL-3.0）");
  QCommandLineOption mock_opt("mock", "使用内置 mock 电台（M0 默认且唯一选项）");
  QCommandLineOption headless_opt("headless", "无界面总线模式（M5 实现，当前占位）");
  QCommandLineOption bus_opt({"b", "bus"}, "总线端口（配合 --headless，M5）", "port");
  cli.addOption(mock_opt);
  cli.addOption(headless_opt);
  cli.addOption(bus_opt);
  cli.addHelpOption();
  cli.process(app);

  if (cli.isSet(headless_opt) || cli.isSet(bus_opt))
    qWarning() << "--headless/--bus 将在 M5 落地，M0 忽略并进入 GUI(mock)";

  // M0 唯一 transport：mock。M1 接入 serial/udp 后按参数选择。
  auto transport = std::make_shared<radio::core::MockTransport>();

  MainWindow win(transport);
  win.resize(1280, 720);
  win.show();
  return app.exec();
}
