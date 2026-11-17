// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/keep_alive.h"

#if defined(Q_OS_ANDROID)
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>  // QNativeInterface::QAndroidApplication
#endif

namespace radio::app {
#if defined(Q_OS_ANDROID)
namespace {

// android.net.wifi.WifiManager.WIFI_MODE_FULL_HIGH_PERF（API 12 起）。
// 不用 WIFI_MODE_FULL_LOW_LATENCY：那个是「保持能唤醒」，这个才是「别打盹」。
constexpr jint kWifiModeFullHighPerf = 4;
// android.os.PowerManager.PARTIAL_WAKE_LOCK：唯一没被废弃的级别，值就是 0。
// 明写常量而不是传 0，是为了让人一眼看出这里刻意不点亮屏幕。
constexpr jint kPartialWakeLock = 0;

// Context.WIFI_SERVICE / POWER_SERVICE 的字面值。
constexpr const char kWifiService[] = "wifi";
constexpr const char kPowerService[] = "power";

constexpr const char kTag[] = "radio-suite-listen";

// 两个返回类型都写成设备里的真值，不是「看着像」：
//   createWifiLock → android.net.wifi.WifiManager$WifiLock（嵌套类，没有独立的
//                    android/net/wifi/WifiLock）
//   newWakeLock    → android.os.PowerManager$WakeLock（android/os/，不是
//                    android/os/power/——那是另一个类，同名不同门）
// 2026-11-16 在这台 M2012K11AC 上照 adb pull 回来的 framework-wifi.dex /
// framework.dex 逐条 dexdump 核对过；此前两处都写错，JNI 直接 NoSuchMethodError，
// 桌面 263 项测试与交叉编译全都发现不了。
// Wi-Fi 已进 mainline APEX（/apex/com.android.wifi），签名不在这台机的
// framework.jar 里，查的时候别只 dump framework.jar。
constexpr const char kWifiLockSig[] =
    "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;";
constexpr const char kWakeLockSig[] =
    "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;";

// 把挂着的 Java 异常换成可读文本并清掉。台账要能自己说清是哪一步、为什么没成，
// 光写「抛异常」等于把真机唯一多出来的信息丢掉了。
bool take_java_exception(QString* out) {
  if (out == nullptr) return false;
  QJniEnvironment env;
  jthrowable exc = env->ExceptionOccurred();
  if (exc == nullptr) return false;
  env->ExceptionClear();
  const QJniObject throwable(exc);
  const QJniObject text =
      throwable.callObjectMethod("toString", "()Ljava/lang/String;");
  *out = text.isValid() ? text.toString() : QStringLiteral("<异常无文本>");
  return true;
}

// 为什么不用 QJniObject::callObjectMethod：它会替调用方把挂起的 Java 异常打印到
// logcat 并清掉，等我们回头检查时异常已经不存在了，于是台账只能说「返回空对象」，
// 把真原因（这台 ROM 拒绝 level 0）说成另一个原因；void 版更糟——acquire() 抛异常
// 会被当成成功，锁没拿到却报「已持有」。这里直接用 JNIEnv 调，异常自己接。
QJniObject jni_call_int_string(const QJniObject& target, const char* method,
                               const char* sig, jint first,
                               const QJniObject& second, QString* failure) {
  QJniEnvironment env;
  jclass cls = env->GetObjectClass(target.object());
  if (cls == nullptr) {
    *failure = QStringLiteral("取不到目标对象的类");
    return {};
  }
  jmethodID mid = env->GetMethodID(cls, method, sig);
  env->DeleteLocalRef(cls);
  if (mid == nullptr) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    *failure = QStringLiteral("查不到方法 %1%2")
                   .arg(QString::fromUtf8(method), QString::fromUtf8(sig));
    return {};
  }
  jobject raw =
      env->CallObjectMethod(target.object(), mid, first, second.object());
  QString why;
  if (take_java_exception(&why)) {
    *failure = why;
    return {};
  }
  if (raw == nullptr) {
    *failure = QStringLiteral("返回 null");
    return {};
  }
  return QJniObject(raw);
}

bool jni_call_void(const QJniObject& target, const char* method,
                   const char* sig, QString* failure) {
  QJniEnvironment env;
  jclass cls = env->GetObjectClass(target.object());
  if (cls == nullptr) {
    *failure = QStringLiteral("取不到目标对象的类");
    return false;
  }
  jmethodID mid = env->GetMethodID(cls, method, sig);
  env->DeleteLocalRef(cls);
  if (mid == nullptr) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    *failure = QStringLiteral("查不到方法 %1%2")
                   .arg(QString::fromUtf8(method), QString::fromUtf8(sig));
    return false;
  }
  env->CallVoidMethod(target.object(), mid);
  QString why;
  if (take_java_exception(&why)) {
    *failure = why;
    return false;
  }
  return true;
}

}  // namespace
#endif

bool KeepAlive::required() {
#if defined(Q_OS_ANDROID)
  return true;
#else
  return false;
#endif
}

KeepAlive::~KeepAlive() {
  releaseWifi();
  releaseWake();
}

#if defined(Q_OS_ANDROID)

void KeepAlive::releaseWifi() {
  if (!wifi_held_) return;
  wifi_lock_.callMethod<void>("release", "()V");
  QJniEnvironment env;
  env.checkAndClearExceptions();
  wifi_lock_ = QJniObject();
  wifi_held_ = false;
}

void KeepAlive::releaseWake() {
  if (!wake_held_) return;
  wake_lock_.callMethod<void>("release", "()V");
  QJniEnvironment env;
  env.checkAndClearExceptions();
  wake_lock_ = QJniObject();
  wake_held_ = false;
}

bool KeepAlive::acquire(QString* error) {
  if (held()) return true;

  const QJniObject context = QNativeInterface::QAndroidApplication::context();
  if (!context.isValid()) {
    if (error) *error = QStringLiteral("取不到 Android Context，保活锁无法申请");
    return false;
  }
  const QJniObject tag = QJniObject::fromString(QString::fromUtf8(kTag));
  last_error_.clear();  // 每轮重新攒，别把上一轮的失败挂在这一轮的成果上

  // 两把锁各自独立申请，所以失败原因要能并存（缺哪把、为什么，台账都得说得出）。
  auto note = [this](const QString& msg) {
    last_error_ = last_error_.isEmpty()
                      ? msg
                      : last_error_ + QStringLiteral("；") + msg;
  };

  auto service = [&context](const char* name) {
    const QJniObject service_name = QJniObject::fromString(QString::fromUtf8(name));
    return context.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
        service_name.object());
  };

  // 1) WifiLock(WIFI_MODE_FULL_HIGH_PERF)：让 Wi-Fi 射频别进省电轮询——管「收得到」。
  auto grab_wifi = [this, &service, &tag, &note]() -> bool {
    if (wifi_held_) return true;
    QString why;
    const QJniObject wifi = service(kWifiService);
    if (!wifi.isValid()) {
      note(QStringLiteral("getSystemService(wifi) 返回空"));
      return false;
    }
    wifi_lock_ = jni_call_int_string(wifi, "createWifiLock", kWifiLockSig,
                                     kWifiModeFullHighPerf, tag, &why);
    if (!why.isEmpty()) {
      note(QStringLiteral("createWifiLock 失败：%1").arg(why));
      wifi_lock_ = QJniObject();
      return false;
    }
    // acquire/release 严格 1:1（上面有 wifi_held_ 挡住重复 acquire），所以引用计数
    // 保持默认的 true 也一定会归零，不需要再动 setReferenceCounted。
    if (!jni_call_void(wifi_lock_, "acquire", "()V", &why)) {
      note(QStringLiteral("WifiLock.acquire() 失败：%1").arg(why));
      wifi_lock_ = QJniObject();
      return false;
    }
    wifi_held_ = true;
    return true;
  };

  // 2) PARTIAL_WAKE_LOCK：让 CPU 醒着，事件循环才继续派发 readyRead——管「读得走」。
  auto grab_wake = [this, &service, &tag, &note]() -> bool {
    if (wake_held_) return true;
    QString why;
    const QJniObject power = service(kPowerService);
    if (!power.isValid()) {
      note(QStringLiteral("getSystemService(power) 返回空"));
      return false;
    }
    // 不猜原因：这台 M2012K11AC（MIUI V140 / API 33）上 newWakeLock(PARTIAL=0) 会被
    // 框架自己拒（validateWakeLockParameters 认的 level 集合里没有 0），而 AOSP 认 0。
    // 权限在 manifest 里是齐的，所以把 Java 侧原话回显出来，别替它编一个更顺口的锅。
    wake_lock_ = jni_call_int_string(power, "newWakeLock", kWakeLockSig,
                                     kPartialWakeLock, tag, &why);
    if (!why.isEmpty()) {
      note(QStringLiteral("newWakeLock 失败：%1").arg(why));
      wake_lock_ = QJniObject();
      return false;
    }
    if (!jni_call_void(wake_lock_, "acquire", "()V", &why)) {
      note(QStringLiteral("WakeLock.acquire() 失败：%1").arg(why));
      wake_lock_ = QJniObject();
      return false;
    }
    wake_held_ = true;
    return true;
  };

  const bool wifi_ok = grab_wifi();
  const bool wake_ok = grab_wake();
  if (wifi_ok && wake_ok) return true;
  // 半保活不叫「已保活」——held() 要两把都在，台账不会因此说谎；
  // 但拿着的那把继续拿着，比因为一把没拿到就把它也放掉更接近「尽量活着」。
  // 两把都试（不短路）是这次换来的：WifiLock 签名写错时，&& 让 WakeLock
  // 连被试过都没有，一个拼写错误把两种保护全变成 0。
  if (error) *error = last_error_;
  return false;
}

void KeepAlive::release() {
  releaseWake();
  releaseWifi();
}

#else  // 非 Android：两把锁都不存在，别谎报持有

void KeepAlive::releaseWifi() {}
void KeepAlive::releaseWake() {}

bool KeepAlive::acquire(QString* error) {
  Q_UNUSED(error);
  return true;  // 非 Android 不需要保活；held() 保持 false，别谎报持有
}

void KeepAlive::release() {}

#endif

}  // namespace radio::app
