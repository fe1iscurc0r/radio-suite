// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · ui_common/app_settings.h — 唯一的设置读写入口
// 默认构造的 QSettings 用 NativeFormat，而 Android 上**没有** NativeFormat 后端：
//  setValue 不报错、重启后全是默认值。持久化一律走这里，别再各自构造。
#pragma once

#include <QSettings>
#include <QString>

namespace radio::ui {

// 组织名/应用名写死：桌面（注册表 / plist / ini）落在
// HKCU\Software\radio-suite\radio-suite，与旧的默认构造同一路径，
// 且不依赖宿主进程调过 setOrganizationName（logbook 就没调）。
inline QSettings appSettings() {
#ifdef Q_OS_ANDROID
  return QSettings(QSettings::IniFormat, QSettings::UserScope,
                   QStringLiteral("radio-suite"), QStringLiteral("radio-suite"));
#else
  return QSettings(QStringLiteral("radio-suite"), QStringLiteral("radio-suite"));
#endif
}

}  // namespace radio::ui
