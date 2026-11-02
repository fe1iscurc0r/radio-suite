// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/device_report.h — 链路与权限台账
//
// 为什么要这一层：验收清单 §2b 那张「谁声明的 / 真机状态 / 结论」的表一直只在文档里，
// 用户和接手人都得连 adb 才能看到真相。这里把同样的判定搬进 app，让手机上一屏就能
// 回答「这条功能为什么在你这台机器上不可用」。
//
// 分工：`buildDeviceReport()` 是纯函数（输入=已经拿到的事实），所以能离屏跑用例；
// 事实本身由 `fillAndroidFacts()`（JNI，非 Android 空操作）和调用方填。
#pragma once

#include <QString>
#include <QVector>

namespace radio::app {

enum class ProbeState {
  Ok,             // 可用
  Warn,           // 能用但降级（要说清降成什么样）
  Missing,        // 该有而没有（缺陷）
  NotApplicable,  // 本平台/本构建不适用
};

struct ReportRow {
  QString group;   // 分组：设备 / 权限 / 电台链路 / WSJT-X / 音频 / 数据
  QString item;
  ProbeState state = ProbeState::Ok;
  QString note;    // 一行话：为什么是这个状态、退路是什么
};

QString probeStateText(ProbeState state);

struct ReportInput {
  QString platform;      // QSysInfo::productType()，如 "android" / "windows"
  QString device_model;  // Android: Build.MODEL；其它平台留空
  QString abi;           // Android: Build.SUPPORTED_ABIS[0]
  int android_api = 0;   // 0 = 非 Android
  QString storage_dir;   // 设置实际落点（QSettings::fileName()）

  bool mic_granted = false;
  bool multicast_granted = false;
  bool usb_host_feature = false;

  bool rig_udp_built = false;       // 这个构建里编进了 UDP 传输
  bool rig_tcp_built = false;       // 编进了 CI-V over TCP 传输
  bool rig_bridge_available = false;  // 本机跑得动 Python 桥
  bool rig_serial_built = false;    // 编进了 Qt6::SerialPort
  bool rig_http_bus_built = false;  // 编进了 HTTP 总线 / rigctld 服务端
  QString link_name;                // linkName(Link)：mock / udp / bridge
  QString rig_host;
  // 链路活性由 session 判（阈值只活在那一处），这里只转述：
  // rig_reply_age_ms = -1 表示这条链路至今没给过一个能解析的回帧。
  bool rig_link_alive = false;
  qint64 rig_reply_age_ms = -1;

  bool wsjtx_listening = false;
  QString wsjtx_group;
  quint16 wsjtx_port = 0;
  bool multicast_lock_held = false;
  QString wsjtx_last_error;  // 上一次 start() 失败的原因，成功则空

  bool net_audio_active = false;
  int audio_output_devices = 0;

  bool logbook_available = false;
  bool logbook_has_rows = false;
  QString logbook_path;
  QString logbook_error;
};

// 只做判定与措辞，不碰任何系统调用。
QVector<ReportRow> buildDeviceReport(const ReportInput& in);

// Android：读 Build.* / 权限授予 / 系统特性。非 Android 什么都不填（保持默认值，
// 台账据此显示「本平台不适用」，绝不拿桌面的假数据冒充真机）。
void fillAndroidFacts(ReportInput& in);

}  // namespace radio::app
