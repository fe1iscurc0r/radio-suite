// SPDX-License-Identifier: AGPL-3.0-or-later
// Wi-Fi 组播锁的平台语义 + WsjtxClient 的取锁时机。
// 非 Android 三平台 CI 上跑的都是「本平台不适用」这条分支；Android 上真取到锁
// 只能真机验（判据见 docs/android-真机验收清单.md §2b）。
#include <catch2/catch_test_macros.hpp>

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
