// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/multicast_lock.h"

#if defined(Q_OS_ANDROID)
#include <QJniEnvironment>
#include <QtCore/qcoreapplication_platform.h>  // QNativeInterface::QAndroidApplication
#endif

namespace radio::app {

bool MulticastLock::required() {
#if defined(Q_OS_ANDROID)
  return true;
#else
  return false;
#endif
}

MulticastLock::~MulticastLock() { release(); }

bool MulticastLock::acquire(QString* error) {
#if defined(Q_OS_ANDROID)
  if (held_) return true;

  const QJniObject context = QNativeInterface::QAndroidApplication::context();
  if (!context.isValid()) {
    if (error) *error = QStringLiteral("取不到 Android Context，组播锁无法申请");
    return false;
  }

  // Context.WIFI_SERVICE 的字面值就是 "wifi"。
  const QJniObject service_name = QJniObject::fromString(QStringLiteral("wifi"));
  const QJniObject wifi = context.callObjectMethod(
      "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
      service_name.object());
  if (!wifi.isValid()) {
    if (error) *error = QStringLiteral("getSystemService(wifi) 返回空");
    return false;
  }

  const QJniObject tag =
      QJniObject::fromString(QStringLiteral("radio-suite-wsjtx"));
  const QJniObject lock = wifi.callObjectMethod(
      "createMulticastLock",
      "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;",
      tag.object());
  QJniEnvironment env;
  if (env.checkAndClearExceptions()) {
    if (error)
      *error = QStringLiteral(
          "createMulticastLock 抛异常——十有八九是 manifest 漏了 "
          "CHANGE_WIFI_MULTICAST_STATE");
    return false;
  }
  if (!lock.isValid()) {
    if (error) *error = QStringLiteral("createMulticastLock 返回空对象");
    return false;
  }

  // 引用计数关掉：只有一个持有者，release 一次就该真放下。
  lock.callMethod<void>("setReferenceCounted", "(Z)V", jboolean(false));
  lock.callMethod<void>("acquire", "()V");
  if (env.checkAndClearExceptions()) {
    if (error) *error = QStringLiteral("MulticastLock.acquire() 抛异常");
    return false;
  }

  lock_ = lock;
  held_ = true;
  return true;
#else
  Q_UNUSED(error);
  return true;  // 非 Android 不需要锁；held() 保持 false，别谎报持有
#endif
}

void MulticastLock::release() {
#if defined(Q_OS_ANDROID)
  if (!held_) return;
  lock_.callMethod<void>("release", "()V");
  QJniEnvironment env;
  env.checkAndClearExceptions();
  lock_ = QJniObject();
#endif
  held_ = false;
}

}  // namespace radio::app
