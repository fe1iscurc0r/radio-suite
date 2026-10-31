// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · ui_common/app_settings.h — 唯一的设置读写入口
// 默认构造的 QSettings 用 NativeFormat，而 Android 上**没有** NativeFormat 后端：
//  setValue 不报错、重启后全是默认值。持久化一律走这里，别再各自构造。
#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace radio::ui {

#ifdef Q_OS_ANDROID
// Android 上可选的落盘点实测只有这两个能用：
//  · QCoreApplication::applicationDirPath() 是 **APK 解出来的 native 库目录**
//    （/data/app/~~xxx==/pkg-yyy==/lib/arm64），root 属主 755，应用连临时文件都建不出来，
//    QSettings 的原子替换（写 .tmp 再 rename）必然失败，而且是静默的。
//  · HOME / XDG_CONFIG_HOME 在应用进程的环境里根本不存在（看 /proc/<pid>/environ），
//    所以 IniFormat + UserScope 也解析不出可写路径。
// Qt 在启动时把工作目录设成应用自己的 files 目录，QStandardPaths 也按同一套走，
// 两个候选都试，取第一个真能写的。
inline QString settingsDir() {
  QStringList candidates;
  candidates << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
             << QDir::currentPath();
  for (const QString& d : candidates) {
    if (d.isEmpty()) continue;
    QDir().mkpath(d);
    if (QFileInfo(d).isWritable()) return d;
  }
  return QStringLiteral(".");
}
#endif

// 组织名/应用名写死：桌面（注册表 / plist / ini）落在
// HKCU\Software\radio-suite\radio-suite，与旧的默认构造同一路径，
// 且不依赖宿主进程调过 setOrganizationName（logbook 就没调）。
inline QSettings appSettings() {
#ifdef Q_OS_ANDROID
  return QSettings(QDir(settingsDir()).filePath(QStringLiteral("radio-suite.ini")),
                   QSettings::IniFormat);
#else
  return QSettings(QStringLiteral("radio-suite"), QStringLiteral("radio-suite"));
#endif
}

}  // namespace radio::ui
