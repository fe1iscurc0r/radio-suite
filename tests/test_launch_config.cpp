// SPDX-License-Identifier: AGPL-3.0-or-later
// 链路选择（K1/K3 的回归网）：命令行/持久化偏好的优先级、降级到 mock 的条件、
// 偏好读写往返、状态栏文案与真实链路同源。Android 上没有命令行输入口，
// 「持久化偏好 → transport」这条路必须在离屏用例里钉住，不能等真机才发现跑不通。
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QMetaObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QString>

#include "app/connect_dialog.h"
#include "app/launch_config.h"
#include "core/transport.h"
#include "core/transport_mock.h"
#ifdef RADIO_SUITE_HAVE_UDP
#include "core/transport_udp.h"
#endif
#include "ui_common/app_settings.h"

namespace {
using namespace radio::app;

QApplication& app() {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  static int argc = 1;
  static char name[] = "radio-launch-config-tests";
  static char* argv[] = {name, nullptr};
  static QApplication instance(argc, argv);
  return instance;
}

LinkPrefs udpPrefs(bool explicit_cli = true, std::string host = "192.0.2.7") {
  LinkPrefs p;
  p.link = Link::Udp;
  p.explicit_cli = explicit_cli;
  p.host = std::move(host);
  p.user = "linnan";
  return p;
}

}  // namespace

// 用例名一律 ASCII：catch_discover_tests 把带中文的用例名写进 CTestTestfile 后，
// ctest 传参时会被控制台代码页二次编码，报 "No test cases matched"（本机实测）。
TEST_CASE("resolveLink_cli_statement_wins_over_saved", "[link]") {
  app();
  LinkPrefs cli;
  cli.link = Link::Mock;
  cli.explicit_cli = true;
  // 桌面上「上次连过的电台」不该吃掉这次的 --mock
  const auto got = resolveLink(cli, udpPrefs());
  CHECK(got.link == Link::Mock);
}

// Android 没有命令行输入口，这条是手机上唯一能连真机的路
TEST_CASE("resolveLink_saved_prefs_take_effect_without_cli", "[link]") {
  app();
  const auto got = resolveLink(LinkPrefs{}, udpPrefs(/*explicit_cli*/ false));
  CHECK(got.link == Link::Udp);
  CHECK(got.host == "192.0.2.7");
}

TEST_CASE("resolveLink_falls_back_to_mock_instead_of_guessing_ip", "[link]") {
  app();
  CHECK(resolveLink(LinkPrefs{}, LinkPrefs{}).link == Link::Mock);
  // 偏好里选了 UDP 但 IP 是空的：宁可 mock，也不对着空主机建链
  const auto blank = resolveLink(LinkPrefs{},
                                 udpPrefs(false, std::string{}));
  CHECK(blank.link == Link::Mock);
  CHECK(blank.host.empty());
}

// K3：状态栏那句 "transport: mock" 原先是硬编码，真连上 UDP 也这么写
TEST_CASE("link_status_text_tracks_the_actual_link", "[link]") {
  app();
  CHECK(linkStatusText(LinkPrefs{}).contains("mock"));
  const QString udp = linkStatusText(udpPrefs());
  CHECK(udp.contains("udp"));
  CHECK_FALSE(udp.contains("mock"));
  CHECK(udp.contains("192.0.2.7"));
  CHECK(QString::fromLatin1(linkName(Link::PyBridge)) == "python-bridge");
}

TEST_CASE("mode_index_and_link_mapping_without_python_bridge", "[link]") {
  app();
  CHECK(linkFromModeIndex(0, true) == static_cast<int>(Link::Mock));
  CHECK(linkFromModeIndex(1, true) == static_cast<int>(Link::Udp));
  CHECK(linkFromModeIndex(2, true) == static_cast<int>(Link::PyBridge));
  // Android：下拉框只有两档，越界也不许冒出 PyBridge
  CHECK(linkFromModeIndex(2, false) == static_cast<int>(Link::Udp));
  CHECK(modeIndexFromLink(Link::Mock, true) == 0);
  CHECK(modeIndexFromLink(Link::Udp, true) == 1);
  CHECK(modeIndexFromLink(Link::PyBridge, true) == 2);
  CHECK(modeIndexFromLink(Link::PyBridge, false) == 1);
  for (bool bridge : {true, false}) {
    for (int i = 0; i <= 2; ++i) {
      if (i == 2 && !bridge) continue;  // 没有 Python 桥的下拉框根本没有第 3 档
      const int link = linkFromModeIndex(i, bridge);
      CHECK(modeIndexFromLink(static_cast<Link>(link), bridge) == i);
    }
  }
}

// Android 的 QSettings 默认格式（NativeFormat）没有后端：偏好必须走显式 IniFormat
TEST_CASE("link_prefs_round_trip_through_explicit_ini", "[link]") {
  app();
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  QSettings st(dir.filePath("radio.conf"), QSettings::IniFormat);
  LinkPrefs p = udpPrefs(false);
  p.pass = "s3cr3t";
  p.python_exe = "D:/python/python.exe";
  saveLinkPrefs(st, p);  // 内部已 sync
  const auto back = loadLinkPrefs(st);
  CHECK(back.link == Link::Udp);
  CHECK(back.host == "192.0.2.7");
  CHECK(back.user == "linnan");
  CHECK(back.pass == "s3cr3t");
  CHECK(back.python_exe == "D:/python/python.exe");
  // explicit_cli 是运行期标记，不进设置文件
  CHECK(back.explicit_cli == false);

  QSettings empty(dir.filePath("nothing.conf"), QSettings::IniFormat);
  CHECK(loadLinkPrefs(empty).link == Link::Mock);
}

// 全站唯一的设置入口必须自己把 org/app 说清楚：主窗、wsjtx 面板、字体/代理对话框
// 原先各写一遍默认构造，一旦宿主没调 setOrganizationName（logbook 就没调）就散到
// 另一个存储里；Android 上 NativeFormat 更是直接静默丢失。
TEST_CASE("app_settings_names_its_own_store", "[settings]") {
  app();
  const QSettings st = radio::ui::appSettings();
  CHECK(st.scope() == QSettings::UserScope);
  CHECK(st.organizationName() == QLatin1String("radio-suite"));
  CHECK(st.applicationName() == QLatin1String("radio-suite"));
#ifdef Q_OS_ANDROID
  CHECK(st.format() == QSettings::IniFormat);
#endif
}

// 只建对象不 open：断言工厂按偏好选对了 transport 类型
TEST_CASE("make_transport_builds_the_link_the_prefs_ask_for", "[link]") {
  app();
  auto mock = makeTransport(LinkPrefs{});
  REQUIRE(mock);
  CHECK(dynamic_cast<radio::core::MockTransport*>(mock.get()) != nullptr);
#ifdef RADIO_SUITE_HAVE_UDP
  auto udp = makeTransport(resolveLink(udpPrefs(), LinkPrefs{}));
  REQUIRE(udp);
  CHECK(dynamic_cast<radio::core::MockTransport*>(udp.get()) == nullptr);
  // IP 空 → 退回 mock（和 resolveLink 同一套判定）
  auto blank = makeTransport(udpPrefs(true, std::string{}));
  CHECK(dynamic_cast<radio::core::MockTransport*>(blank.get()) != nullptr);
#endif
}

TEST_CASE("connect_dialog_prefills_from_prefs_and_returns_them_on_save",
          "[link]") {
  app();
  ConnectDialog dlg(udpPrefs(/*explicit_cli*/ false), nullptr);
  auto* mode = dlg.findChild<QComboBox*>();
  auto* host = dlg.findChild<QLineEdit*>();
  REQUIRE(mode != nullptr);
  REQUIRE(host != nullptr);
  CHECK(mode->currentIndex() == 1);
  CHECK(host->text() == "192.0.2.7");
  // 「保存」= accept：调用方只在 Accepted 时取 prefs()
  REQUIRE(dlg.result() == QDialog::Rejected);
  REQUIRE(QMetaObject::invokeMethod(&dlg, "onSave"));
  CHECK(dlg.result() == QDialog::Accepted);
  CHECK(dlg.prefs().link == Link::Udp);
  CHECK(dlg.prefs().host == "192.0.2.7");
  // 表单是当面表态，优先级等同命令行
  CHECK(dlg.prefs().explicit_cli == true);
}
