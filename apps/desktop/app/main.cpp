// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/main.cpp — 入口
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <memory>

#include "app/MainWindow.h"
#include "app/launch_config.h"
#include "app/session.h"
#include "ui_common/app_settings.h"
#include "ui_common/font_proxy_settings.h"
#ifdef RADIO_SUITE_HAVE_BUS
#include "bus/bus_server.h"
#include "bus/rigctld_server.h"
#endif

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("radio-suite");
  QApplication::setApplicationVersion("0.1.0");
  QApplication::setOrganizationName("radio-suite");

  // 启动即应用持久化的字体与代理（原版 FontManager/ProxyManager 语义）
  radio::ui::FontSettingsDialog::applySaved();
  radio::ui::ProxySettingsDialog::applySaved();

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

  // 链路选择：命令行 > 持久化偏好（Android 没命令行，只能走这条）> mock
  const radio::app::LinkPrefs cli_prefs = radio::app::linkPrefsFromCli(
      cli.isSet(mock_opt), cli.isSet(radio_opt), cli.isSet(radio_py_opt),
      cli.value(radio_opt).toStdString(), cli.value(user_opt).toStdString(),
      cli.value(pass_opt).toStdString(),
      cli.isSet(python_opt) ? cli.value(python_opt).toStdString() : std::string{});
  const radio::app::LinkPrefs prefs =
      radio::app::resolveLink(cli_prefs, radio::app::loadLinkPrefs());

  if (cli.isSet(headless_opt) || cli.isSet(bus_opt)) {
    // ---- headless 总线模式（scratchpad/lumo 集成，M5 第一片）----
#ifdef RADIO_SUITE_HAVE_BUS
    uint16_t port = static_cast<uint16_t>(cli.value(bus_opt).toInt());
    if (port == 0) port = 8765;
    std::shared_ptr<radio::core::ICivTransport> transport =
        radio::app::makeTransport(prefs);
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
    // rigctld（rigplane 语义移植）：WSJT-X/fldigi 即插
#ifdef RADIO_SUITE_HAVE_BUS
    radio::bus::RigctldServer rigctld(session);
    radio::bus::RigctldServer::Config rcfg;
    QString rerr;
    if (!rigctld.start(rcfg, &rerr)) qWarning() << rerr;
#endif
    qInfo() << "headless 总线模式运行中，Ctrl+C 退出";
    return app.exec();
#else
    qCritical() << "本构建未含 HTTP 总线（缺 Qt6::HttpServer）";
    return 1;
#endif
  }

  std::shared_ptr<radio::core::ICivTransport> transport =
      radio::app::makeTransport(prefs);
  const char* origin =
      cli_prefs.explicit_cli              ? "命令行"
      : prefs.link == radio::app::Link::Mock ? "默认"
                                             : "持久化偏好";
  qInfo() << "链路：" << radio::app::linkName(prefs.link) << "（来源：" << origin << "）";
#ifdef Q_OS_ANDROID
  // 设备上「设置没保住」是最难查的一类问题，而 Android 的可选落盘点里有两个是写不进去
  // 的（APK 的 lib 目录、HomeScope 解析出的 XDG 路径），所以把真实路径打在启动日志里。
  qInfo() << "设置文件：" << radio::ui::appSettings().fileName();
#endif

  MainWindow win(transport, prefs);
  win.resize(1280, 720);
  win.show();
  return app.exec();
}
