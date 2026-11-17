// SPDX-License-Identifier: AGPL-3.0-or-later
// Wi-Fi 组播锁 / 锁屏保活锁的平台语义 + WsjtxClient 的取锁时机。
// 非 Android 三平台 CI 上跑的都是「本平台不适用」这条分支；Android 上真取到锁
// 只能真机验（判据见 docs/android-真机验收清单.md §2b）。
#include <catch2/catch_test_macros.hpp>

#include "app/keep_alive.h"
#include "app/multicast_lock.h"
#include "app/wsjtx_client.h"
#include "qt_test_app.h"

TEST_CASE("multicast lock: platform without the filter reports not-applicable",
          "[wsjtx][lock]") {
#if defined(Q_OS_ANDROID)
  SKIP("Android 上这把锁是必需的，语义交给真机验收");
#else
  CHECK_FALSE(radio::app::MulticastLock::required());
  radio::app::MulticastLock lock;
  QString error;
  CHECK(lock.acquire(&error));  // 不需要锁 ⇒ 视为成功
  CHECK(error.isEmpty());
  CHECK_FALSE(lock.held());     // 但绝不谎报「已持有」
  lock.release();
  CHECK_FALSE(lock.held());
#endif
}

TEST_CASE("wsjtx client: unicast listening never takes the lock",
          "[wsjtx][ui][lock]") {
  qtTestApp();
  radio::app::WsjtxClient client;
  QString error;
  REQUIRE(client.start("127.0.0.1", 0, &error));
  CHECK(client.isListening());
  CHECK_FALSE(client.multicastLockHeld());
  client.stop();
  CHECK_FALSE(client.isListening());
  CHECK_FALSE(client.multicastLockHeld());
}

TEST_CASE("wsjtx client: a failed start keeps the reason until the next try",
          "[wsjtx][ui][lock]") {
  qtTestApp();
  radio::app::WsjtxClient client;
  QString error;
  // 地址非法：bind 会成功，卡在 QHostAddress 解析这一步，失败原因是确定的
  CHECK_FALSE(client.start("not-an-address", 0, &error));
  CHECK_FALSE(client.isListening());
  CHECK(client.lastError() == error);
  CHECK_FALSE(client.lastError().isEmpty());
  // 下一次成功必须把旧原因清掉，否则台账会一直挂着上次的错
  REQUIRE(client.start("127.0.0.1", 0, &error));
  CHECK(client.lastError().isEmpty());
  client.stop();
}

TEST_CASE("wsjtx client: restart does not leak the lock", "[wsjtx][ui][lock]") {
  qtTestApp();
  radio::app::WsjtxClient client;
  QString error;
  for (int i = 0; i < 3; ++i) {
    REQUIRE(client.start("127.0.0.1", 0, &error));
    client.stop();
  }
  CHECK_FALSE(client.multicastLockHeld());
}

TEST_CASE("keep alive: platform without sleep-drain reports not-applicable",
          "[wsjtx][lock][keepalive]") {
#if defined(Q_OS_ANDROID)
  SKIP("Android 上这两把锁是必需的，语义交给真机验收（清单 §2b N1 保活）");
#else
  CHECK_FALSE(radio::app::KeepAlive::required());
  radio::app::KeepAlive keep;
  QString error;
  CHECK(keep.acquire(&error));   // 不需要 ⇒ 视为成功
  CHECK(error.isEmpty());
  // 但三谓词一律不许谎报：台账上「已保活」必须是真持有
  CHECK_FALSE(keep.held());
  CHECK_FALSE(keep.wifiLockHeld());
  CHECK_FALSE(keep.wakeLockHeld());
  keep.release();
  CHECK_FALSE(keep.wifiLockHeld());
  CHECK_FALSE(keep.wakeLockHeld());
  keep.release();  // 没拿过再放一次，不许炸
#endif
}

TEST_CASE("wsjtx client: desktop listening never claims the phone keep-alive",
          "[wsjtx][ui][lock][keepalive]") {
  qtTestApp();
  radio::app::WsjtxClient client;
  QString error;
  REQUIRE(client.start("127.0.0.1", 0, &error));
  CHECK(client.isListening());
  CHECK_FALSE(client.keepAliveHeld());
  CHECK_FALSE(client.wifiLockHeld());
  CHECK_FALSE(client.wakeLockHeld());
  // 保活失败与否都不该污染 start() 的失败原因
  CHECK(client.lastError().isEmpty());
  client.stop();
  CHECK_FALSE(client.keepAliveHeld());
}

TEST_CASE("wsjtx client: restart does not leak the keep-alive",
          "[wsjtx][ui][lock][keepalive]") {
  qtTestApp();
  radio::app::WsjtxClient client;
  QString error;
  // 用单播：桌面开发机上 joinMulticastGroup(224.0.0.1) 会因多网卡/VPN 直接失败
  // （代码注释里就写了这个已知现象），那样 REQUIRE 测的是网络环境不是泄漏。
  // Android 上真取锁会不会泄漏，只能真机验（清单 §2b）。
  for (int i = 0; i < 3; ++i) {
    REQUIRE(client.start("127.0.0.1", 0, &error));
    client.stop();
  }
  CHECK_FALSE(client.wifiLockHeld());
  CHECK_FALSE(client.wakeLockHeld());
  CHECK_FALSE(client.keepAliveHeld());
}
