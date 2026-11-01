// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/multicast_lock.h — Android Wi-Fi 组播锁
#pragma once

#include <QString>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

namespace radio::app {

// Android 从 4.x 起默认过滤掉 Wi-Fi 上的组播/广播帧：不持有 MulticastLock，
// `joinMulticastGroup()` 一样会返回成功，但一个包都收不到（成功的是内核，
// 网卡根本没把帧交给系统）。所以「加入组播失败」和「加入成功却没数据」是
// 两个不同的缺陷，后者只能靠这把锁解决。
//
// 非 Android 平台上这把锁不存在也不需要：`required()` 返回 false，
// `acquire()` 直接返回 true 且 **不**把 `held()` 置真——自检页要如实显示
// 「本平台不适用」，不能假装持锁。
class MulticastLock {
 public:
  MulticastLock() = default;
  ~MulticastLock();
  MulticastLock(const MulticastLock&) = delete;
  MulticastLock& operator=(const MulticastLock&) = delete;

  // 这个平台是否必须持锁才收得到组播。
  static bool required();

  // 取锁并 acquire。已经持有时直接返回 true。
  // 失败时返回 false 并把原因写进 error（多半是 manifest 没声明
  // CHANGE_WIFI_MULTICAST_STATE，或 Context 还没就绪）。
  bool acquire(QString* error = nullptr);
  void release();
  bool held() const { return held_; }

 private:
  bool held_ = false;
#if defined(Q_OS_ANDROID)
  QJniObject lock_;
#endif
};

}  // namespace radio::app
