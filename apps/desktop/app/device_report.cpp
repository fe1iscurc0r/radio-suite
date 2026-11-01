// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/device_report.h"

#include <QHostAddress>

#if defined(Q_OS_ANDROID)
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace radio::app {
namespace {

bool isAndroid(const ReportInput& in) {
  return in.platform == QLatin1String("android");
}

ReportRow makeRow(const QString& group, const QString& item, ProbeState state,
                  const QString& note) {
  return ReportRow{group, item, state, note};
}

}  // namespace

QString probeStateText(ProbeState state) {
  switch (state) {
    case ProbeState::Ok: return QStringLiteral("可用");
    case ProbeState::Warn: return QStringLiteral("降级");
    case ProbeState::Missing: return QStringLiteral("缺失");
    case ProbeState::NotApplicable: return QStringLiteral("不适用");
  }
  return QStringLiteral("?");
}

QVector<ReportRow> buildDeviceReport(const ReportInput& in) {
  const bool android = isAndroid(in);
  QVector<ReportRow> rows;

  // ---- 设备 ----
  QString platform_note = in.platform;
  if (android)
    platform_note = QStringLiteral("%1 · API %2 · %3")
                        .arg(in.platform)
                        .arg(in.android_api)
                        .arg(in.abi.isEmpty() ? QStringLiteral("ABI 未知") : in.abi);
  rows << makeRow("设备", "平台", ProbeState::Ok, platform_note);
  if (android && !in.device_model.isEmpty())
    rows << makeRow("设备", "机型", ProbeState::Ok, in.device_model);
  rows << makeRow("设备", "设置落点",
                  in.storage_dir.isEmpty() ? ProbeState::Missing : ProbeState::Ok,
                  in.storage_dir.isEmpty()
                      ? QStringLiteral("拿不到设置落点，偏好与本地库都写不下去")
                      : in.storage_dir);

  // ---- 权限 ----
  if (android) {
    rows << makeRow("权限", "RECORD_AUDIO",
                    in.mic_granted ? ProbeState::Ok : ProbeState::Missing,
                    in.mic_granted
                        ? QStringLiteral("已授予：FT8 本机麦克风与 50003 上行可用")
                        : QStringLiteral("未授予：被永久拒绝时要去系统设置 → 应用 → "
                                         "权限里改，应用内无法再弹"));
    rows << makeRow("权限", "CHANGE_WIFI_MULTICAST_STATE",
                    in.multicast_granted ? ProbeState::Ok : ProbeState::Missing,
                    in.multicast_granted
                        ? QStringLiteral("已授予（normal 权限，装机即给）")
                        : QStringLiteral("未授予：createMulticastLock 会抛 "
                                         "SecurityException，组播收不到包"));
    rows << makeRow("权限", "存储（WRITE/READ_EXTERNAL）", ProbeState::Warn,
                    QStringLiteral("targetSdk 34 上系统不再授予历史遗留的存储权限；"
                                   "导出走 SAF 选择器，私有目录照常写，不需要它"));
    rows << makeRow("权限", "USB Host 特性",
                    in.usb_host_feature ? ProbeState::Ok : ProbeState::Warn,
                    in.usb_host_feature
                        ? QStringLiteral("本机支持 USB Host；串口后端尚未接入，"
                                         "插设备也还没有能读它的代码")
                        : QStringLiteral("本机未报告 android.hardware.usb.host"));
  } else {
    rows << makeRow("权限", "运行时权限", ProbeState::NotApplicable,
                    QStringLiteral("本平台不需要运行时授权，麦克风/组播直接可用"));
  }

  // ---- 电台链路 ----
  const bool mock_link = in.link_name.isEmpty() || in.link_name == QLatin1String("mock");
  rows << makeRow(
      "电台链路", "当前链路",
      mock_link ? ProbeState::Warn : ProbeState::Ok,
      mock_link
          ? QStringLiteral("%1 —— 内置模拟电台，不是真机链路；凡是判电台的条目都不许拿它打勾")
                .arg(in.link_name.isEmpty() ? QStringLiteral("未选择") : in.link_name)
          : QStringLiteral("%1 → %2").arg(in.link_name).arg(in.rig_host));
  rows << makeRow("电台链路", "UDP（RS-BA1）传输",
                  in.rig_udp_built ? ProbeState::Ok : ProbeState::Missing,
                  in.rig_udp_built
                      ? QStringLiteral("已编入；真用还需要电台侧有 RS-BA1 或等价网关")
                      : QStringLiteral("本构建没编 Qt6::Network，只能跑 mock"));
  rows << makeRow("电台链路", "Python 桥（rsba1-core）",
                  android ? ProbeState::NotApplicable
                          : (in.rig_bridge_available ? ProbeState::Ok
                                                     : ProbeState::Missing),
                  android
                      ? QStringLiteral("设备上没有 Python 解释器，这条在安卓恒不可用")
                      : QStringLiteral("桌面后手；依赖 tools/rsba1_bridge.py"));
  rows << makeRow("电台链路", "串口（CI-V / KISS）",
                  in.rig_serial_built ? ProbeState::Ok : ProbeState::Missing,
                  in.rig_serial_built
                      ? QStringLiteral("Qt6::SerialPort 已编入")
                      : (android ? QStringLiteral("安卓 Qt 6.7.3 不带 QtSerialPort；"
                                                  "OTG 串口需要自建 UsbManager 后端，"
                                                  "缺的是后端不是权限弹窗")
                                  : QStringLiteral("本构建没找到 Qt6::SerialPort")));
  rows << makeRow("电台链路", "对外总线（rigctld）",
                  in.rig_http_bus_built ? ProbeState::Ok : ProbeState::NotApplicable,
                  in.rig_http_bus_built
                      ? QStringLiteral("本进程作为 rigctld 服务端对外提供状态")
                      : QStringLiteral("本构建没编进 bus（缺 Qt6::HttpServer），"
                                       "手机端没有对外总线"));

  // ---- WSJT-X ----
  const bool multicast_group =
      QHostAddress(in.wsjtx_group).isMulticast();
  if (!in.wsjtx_listening) {
    const bool has_error = !in.wsjtx_last_error.isEmpty();
    rows << makeRow("WSJT-X", "监听",
                    has_error ? ProbeState::Missing : ProbeState::Warn,
                    has_error
                        ? in.wsjtx_last_error
                        : QStringLiteral("未监听（面板里启动后才会有解码行）"));
    // 启动失败时锁行也要出现：真机上就撞过「锁拿得到、join 失败」，
    // 只在监听成功时才显示锁，等于把最该看的那种情况藏起来。
    if (has_error && android && QHostAddress(in.wsjtx_group).isMulticast())
      rows << makeRow("WSJT-X", "Wi-Fi 组播锁", ProbeState::Warn,
                      QStringLiteral("启动失败后锁已释放（正常收尾）；锁本身能否拿到"
                                     "看 logcat 里本应用 uid 的 acquireMulticastLock"));
  } else {
    rows << makeRow("WSJT-X", "监听", ProbeState::Ok,
                    QStringLiteral("%1:%2").arg(in.wsjtx_group).arg(in.wsjtx_port));
    if (multicast_group) {
      if (android) {
        rows << makeRow("WSJT-X", "Wi-Fi 组播锁",
                        in.multicast_lock_held ? ProbeState::Ok : ProbeState::Missing,
                        in.multicast_lock_held
                            ? QStringLiteral("已持有：组播帧才会交给应用")
                            : QStringLiteral("未持有：此时 joinMulticastGroup 照样"
                                             "返回成功，但一个包都收不到"));
      } else {
        rows << makeRow("WSJT-X", "Wi-Fi 组播锁", ProbeState::NotApplicable,
                        QStringLiteral("本平台没有组播过滤，不需要锁"));
      }
    } else {
      rows << makeRow("WSJT-X", "单播监听", ProbeState::Ok,
                      QStringLiteral("单播不受 Wi-Fi 组播过滤影响，是手机端"
                                     "最先能验通的通路"));
    }
  }

  // ---- 音频 ----
  rows << makeRow("音频", "输出设备枚举",
                  in.audio_output_devices > 0 ? ProbeState::Ok : ProbeState::Missing,
                  QStringLiteral("枚举到 %1 个有输出通道的设备")
                      .arg(in.audio_output_devices));
  rows << makeRow("音频", "网络音频 50003",
                  in.net_audio_active ? ProbeState::Ok : ProbeState::Warn,
                  in.net_audio_active
                      ? QStringLiteral("RX 正在播（是否真出声要耳朵或环回确认）")
                      : QStringLiteral("未运行：没开网络音频时这行本该如此"));

  // ---- 数据 ----
  const QString db_path =
      in.logbook_path.isEmpty() ? QStringLiteral("路径未知") : in.logbook_path;
  QString db_note;
  if (in.logbook_available) {
    db_note = QStringLiteral("库可用（log 表可读） · %1").arg(db_path);
  } else {
    db_note = QStringLiteral("库不可用，「日志」页不建：%1 · %2")
                  .arg(in.logbook_error.isEmpty() ? QStringLiteral("原因未上报")
                                                  : in.logbook_error,
                       db_path);
  }
  rows << makeRow("数据", "HamLog 本地库",
                  in.logbook_available ? ProbeState::Ok : ProbeState::Missing,
                  db_note);
  rows << makeRow("数据", "日志记录",
                  in.logbook_has_rows ? ProbeState::Ok : ProbeState::Warn,
                  in.logbook_has_rows
                      ? QStringLiteral("日志页有内容")
                      : QStringLiteral("还没有一条 QSO（记一条再回来看这行）"));

  return rows;
}

void fillAndroidFacts(ReportInput& in) {
#if defined(Q_OS_ANDROID)
  const QJniObject context = QNativeInterface::QAndroidApplication::context();
  if (!context.isValid()) return;

  in.android_api = QNativeInterface::QAndroidApplication::sdkVersion();
  const QJniObject model = QJniObject::getStaticObjectField(
      "android/os/Build", "MODEL", "Ljava/lang/String;");
  if (model.isValid()) in.device_model = model.toString();

  QJniEnvironment env;
  jclass build = env->FindClass("android/os/Build");
  if (build != nullptr) {
    jfieldID field = env->GetStaticFieldID(build, "SUPPORTED_ABIS",
                                           "[Ljava/lang/String;");
    if (field != nullptr) {
      auto abis =
          reinterpret_cast<jobjectArray>(env->GetStaticObjectField(build, field));
      if (abis != nullptr && env->GetArrayLength(abis) > 0) {
        jstring first =
            reinterpret_cast<jstring>(env->GetObjectArrayElement(abis, 0));
        in.abi = QJniObject(first).toString();
        env->DeleteLocalRef(first);
      }
    }
  }
  if (env->ExceptionCheck()) env->ExceptionClear();

  auto granted = [&context](const char* permission) {
    const QJniObject name = QJniObject::fromString(QString::fromLatin1(permission));
    // PackageManager.PERMISSION_GRANTED == 0
    return context.callMethod<jint>(
               "checkSelfPermission", "(Ljava/lang/String;)I", name.object()) == 0;
  };
  in.mic_granted = granted("android.permission.RECORD_AUDIO");
  in.multicast_granted = granted("android.permission.CHANGE_WIFI_MULTICAST_STATE");

  const QJniObject pm = context.callObjectMethod(
      "getPackageManager", "()Landroid/content/pm/PackageManager;");
  if (pm.isValid()) {
    const QJniObject feature = QJniObject::fromString(
        QStringLiteral("android.hardware.usb.host"));
    in.usb_host_feature = pm.callMethod<jboolean>(
        "hasSystemFeature", "(Ljava/lang/String;)Z", feature.object());
  }
  if (env->ExceptionCheck()) env->ExceptionClear();
#else
  Q_UNUSED(in);
#endif
}

}  // namespace radio::app
