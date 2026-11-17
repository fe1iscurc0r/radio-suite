// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/keep_alive.h — 监听期间的 Wi-Fi 高性能锁 + CPU 唤醒锁
#pragma once

#include <QString>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

namespace radio::app {

// 为什么需要它：`MulticastLock` **只**关掉 Wi-Fi 的组播过滤，不负责让射频和 CPU 醒着。
// 2026-11-15 在 M2012K11AC 上实测过（见 evidence/2026-11-15-android-n1-n3/
// n1-rootcause.json）：屏幕一灭，整机在 IP 层就不收货——休眠态 ICMP 12 发 0 收，
// 亮屏态 60 发 60 收，且亮屏下单播连发 110 秒包一路进内核。
// 也就是说「锁屏挂机听 FT8 / WSJT-X」这个真实场景原来是静默失效的：
// 用户以为在监听，其实一个字都没进来，UI 也不会说。
//
// 所以要两把锁一起拿，缺一不可：
//   · `WifiLock(WIFI_MODE_FULL_HIGH_PERF)` —— 关掉 Wi-Fi 省电轮询（DTIM 打盹），
//     这条管的是**收得到**；
//   · `PARTIAL_WAKE_LOCK` —— 让 CPU 不suspend，事件循环才能继续派发 `readyRead`，
//     这条管的是**读得走**。
// 只拿后者，包会堆在内核里到缓冲打满然后被丢；只拿前者，应用侧不消费。
//
// 状态要分开报：`wifiLockHeld()` 与 `wakeLockHeld()` 各自独立。只成了一把时
// 「亮屏能收、锁屏就断」这个症状和两把都在时不一样，混成一个 held() 就查不出来。
//
// 非 Android 平台上这两把锁不存在也不需要：`required()` 返回 false，
// `acquire()` 直接返回 true 且**不**把 held 置真——自检台账要如实显示「本平台不适用」，
// 不能假装持锁。（与 MulticastLock 同一套口径。）
class KeepAlive {
 public:
  KeepAlive() = default;
  ~KeepAlive();
  KeepAlive(const KeepAlive&) = delete;
  KeepAlive& operator=(const KeepAlive&) = delete;

  static bool required();

  // 两把锁都拿到才返回 true；任何一把失败都把原因写进 error 并回滚已拿到的那把，
  // 不留「半保活」状态——半拿比不拿更坏，因为它会让台账显示「已保活」。
  bool acquire(QString* error = nullptr);
  void release();

  bool held() const { return wifi_held_ && wake_held_; }
  bool wifiLockHeld() const { return wifi_held_; }
  bool wakeLockHeld() const { return wake_held_; }

  // 上一次 acquire 失败的原因（成功后清空）。台账要能在「没保活」时说清为什么，
  // 光靠面板那行红字，用户得正好看见才算传到话——N3 就是这么静默掉的。
  QString lastError() const { return last_error_; }

 private:
  void releaseWifi();
  void releaseWake();

  bool wifi_held_ = false;
  bool wake_held_ = false;
  QString last_error_;
#if defined(Q_OS_ANDROID)
  QJniObject wifi_lock_;
  QJniObject wake_lock_;
#endif
};

}  // namespace radio::app
