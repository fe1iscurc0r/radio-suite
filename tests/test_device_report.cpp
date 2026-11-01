// SPDX-License-Identifier: AGPL-3.0-or-later
// 链路与权限台账：判定与措辞。
// 关键点：`buildDeviceReport()` 是纯函数，所以「Android 上的说法」能在桌面上跑断言，
// 不必等真机；而 `fillAndroidFacts()` 在非 Android 必须是空操作（不许拿桌面数据冒充真机）。
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QVector>

#include "app/device_report.h"
#include "app/self_check_dialog.h"

namespace {

QApplication& app() {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  static int argc = 1;
  static char name[] = "radio-ui-tests";
  static char* argv[] = {name, nullptr};
  static QApplication instance(argc, argv);
  return instance;
}

const radio::app::ReportRow* rowOf(const QVector<radio::app::ReportRow>& rows,
                                   const QString& item) {
  for (const auto& r : rows)
    if (r.item == item) return &r;
  return nullptr;
}

radio::app::ReportInput androidInput() {
  radio::app::ReportInput in;
  in.platform = QStringLiteral("android");
  in.android_api = 34;
  in.abi = QStringLiteral("arm64-v8a");
  in.device_model = QStringLiteral("Redmi K40");
  in.storage_dir = QStringLiteral("/data/data/org.radiosuite.app/files");
  in.mic_granted = true;
  in.multicast_granted = true;
  in.rig_udp_built = true;
  in.link_name = QStringLiteral("udp");
  in.rig_host = QStringLiteral("192.168.1.50");
  in.wsjtx_listening = true;
  in.wsjtx_group = QStringLiteral("224.0.0.1");
  in.wsjtx_port = 2237;
  in.multicast_lock_held = true;
  in.audio_output_devices = 1;
  in.net_audio_active = true;
  in.logbook_available = true;
  in.logbook_has_rows = true;
  in.logbook_path = QStringLiteral("/data/data/org.radiosuite.app/files/HamLog/Log.db");
  return in;
}

}  // namespace

TEST_CASE("device report: android multicast without lock is missing and says join lies", "[report]") {
  auto in = androidInput();
  in.multicast_lock_held = false;
  const auto rows = radio::app::buildDeviceReport(in);
  const auto* lock = rowOf(rows, QStringLiteral("Wi-Fi 组播锁"));
  REQUIRE(lock != nullptr);
  CHECK(lock->state == radio::app::ProbeState::Missing);
  CHECK(lock->note.contains(QStringLiteral("返回成功")));
  CHECK(lock->note.contains(QStringLiteral("一个包都收不到")));
}

TEST_CASE("device report: android multicast with lock is ok", "[report]") {
  const auto rows = radio::app::buildDeviceReport(androidInput());
  const auto* lock = rowOf(rows, QStringLiteral("Wi-Fi 组播锁"));
  REQUIRE(lock != nullptr);
  CHECK(lock->state == radio::app::ProbeState::Ok);
}

TEST_CASE("device report: non-android does not pretend to need the lock", "[report]") {
  auto in = androidInput();
  in.platform = QStringLiteral("windows");
  in.android_api = 0;
  const auto rows = radio::app::buildDeviceReport(in);
  const auto* lock = rowOf(rows, QStringLiteral("Wi-Fi 组播锁"));
  REQUIRE(lock != nullptr);
  CHECK(lock->state == radio::app::ProbeState::NotApplicable);
  // 平台专属的权限行在非安卓上整组换成一行「不适用」
  CHECK(rowOf(rows, QStringLiteral("RECORD_AUDIO")) == nullptr);
  const auto* perm = rowOf(rows, QStringLiteral("运行时权限"));
  REQUIRE(perm != nullptr);
  CHECK(perm->state == radio::app::ProbeState::NotApplicable);
}

TEST_CASE("device report: mock link is degraded not ok", "[report]") {
  auto in = androidInput();
  in.link_name = QStringLiteral("mock");
  const auto rows = radio::app::buildDeviceReport(in);
  const auto* link = rowOf(rows, QStringLiteral("当前链路"));
  REQUIRE(link != nullptr);
  CHECK(link->state == radio::app::ProbeState::Warn);
  CHECK(link->note.contains(QStringLiteral("不许拿它打勾")));
}

TEST_CASE("device report: android serial gap names the missing backend", "[report]") {
  auto in = androidInput();
  in.rig_serial_built = false;
  const auto rows = radio::app::buildDeviceReport(in);
  const auto* serial = rowOf(rows, QStringLiteral("串口（CI-V / KISS）"));
  REQUIRE(serial != nullptr);
  CHECK(serial->state == radio::app::ProbeState::Missing);
  CHECK(serial->note.contains(QStringLiteral("UsbManager")));
}

TEST_CASE("device report: storage row explains targetSdk 34 legacy permission", "[report]") {
  const auto rows = radio::app::buildDeviceReport(androidInput());
  const auto* storage = rowOf(rows, QStringLiteral("存储（WRITE/READ_EXTERNAL）"));
  REQUIRE(storage != nullptr);
  CHECK(storage->state == radio::app::ProbeState::Warn);
  CHECK(storage->note.contains(QStringLiteral("SAF")));
}

TEST_CASE("device report: no bus compiled in is not reported as available", "[report]") {
  auto in = androidInput();
  in.rig_http_bus_built = false;
  const auto rows = radio::app::buildDeviceReport(in);
  const auto* bus = rowOf(rows, QStringLiteral("对外总线（rigctld）"));
  REQUIRE(bus != nullptr);
  CHECK(bus->state == radio::app::ProbeState::NotApplicable);
}

TEST_CASE("self check dialog renders every row and re-reads facts", "[report][ui]") {
  app();
  int calls = 0;
  radio::app::SelfCheckDialog dlg([&calls] {
    ++calls;
    return androidInput();
  });
  const auto expected = radio::app::buildDeviceReport(androidInput());
  CHECK(dlg.rowCount() == static_cast<int>(expected.size()));
  CHECK(dlg.rowCount() > 0);
  const int after_build = calls;
  dlg.refresh();
  dlg.refresh();
  CHECK(calls == after_build + 2);
  // 说明文字与纯函数的输出同源，UI 不再自己写一套判定
  CHECK_FALSE(dlg.noteAt(0).isEmpty());
}

TEST_CASE("fillAndroidFacts is a no-op off android", "[report]") {
#if defined(Q_OS_ANDROID)
  SKIP("真机上的行为由验收清单 §2b 判据核对");
#else
  radio::app::ReportInput in;
  radio::app::fillAndroidFacts(in);
  CHECK(in.android_api == 0);
  CHECK(in.abi.isEmpty());
  CHECK(in.device_model.isEmpty());
  CHECK_FALSE(in.mic_granted);
  CHECK_FALSE(in.multicast_granted);
  CHECK_FALSE(in.usb_host_feature);
#endif
}

TEST_CASE("probe state text covers all four states", "[report]") {
  CHECK(radio::app::probeStateText(radio::app::ProbeState::Ok) ==
        QStringLiteral("可用"));
  CHECK(radio::app::probeStateText(radio::app::ProbeState::Warn) ==
        QStringLiteral("降级"));
  CHECK(radio::app::probeStateText(radio::app::ProbeState::Missing) ==
        QStringLiteral("缺失"));
  CHECK(radio::app::probeStateText(radio::app::ProbeState::NotApplicable) ==
        QStringLiteral("不适用"));
}
