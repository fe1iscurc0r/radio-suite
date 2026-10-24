// SPDX-License-Identifier: AGPL-3.0-or-later
// PTT 二次确认状态机 + 设频门禁测试（TEST_CASE 名保持 ASCII，理由见 test_civ.cpp 头注）
#include <catch2/catch_test_macros.hpp>

#include "core/safety.h"

using namespace radio::core;

TEST_CASE("safety: double-confirm enters TX", "[safety]") {
  SafetyGate g;
  CHECK(g.phase() == PttPhase::Rx);

  CHECK(g.requestTx(1'000));                       // 第一击
  CHECK(g.phase() == PttPhase::ConfirmPending);
  CHECK(g.txDeadlineMs() == 0);                    // 尚无 TX 死限

  CHECK(g.confirmTx(1'000 + 4'999));               // 窗口内第二击
  CHECK(g.phase() == PttPhase::Tx);
  CHECK(g.txDeadlineMs() == 1'000 + 4'999 + SafetyGate::kTxTimeoutMs);
}

TEST_CASE("safety: confirm window expiry cancels", "[safety]") {
  SafetyGate g;
  g.requestTx(1'000);
  CHECK_FALSE(g.confirmTx(1'000 + SafetyGate::kConfirmWindowMs + 1));
  CHECK(g.phase() == PttPhase::Rx);
}

TEST_CASE("safety: tick drives window expiry and 10min auto-RX", "[safety]") {
  SafetyGate g;
  const uint64_t t0 = 100;
  g.requestTx(t0);
  g.tick(t0 + SafetyGate::kConfirmWindowMs + 1);   // 未确认，窗口过期
  CHECK(g.phase() == PttPhase::Rx);

  const uint64_t t1 = t0 + 10'000;
  g.requestTx(t1);
  REQUIRE(g.confirmTx(t1 + 100));
  CHECK(g.phase() == PttPhase::Tx);
  g.tick(t1 + 100 + SafetyGate::kTxTimeoutMs - 1); // 死限前一刻仍 TX
  CHECK(g.phase() == PttPhase::Tx);
  g.tick(t1 + 100 + SafetyGate::kTxTimeoutMs + 1); // 超时自动回 RX
  CHECK(g.phase() == PttPhase::Rx);
  CHECK(g.txDeadlineMs() == 0);
}

TEST_CASE("safety: requestTx ignored while TX; requestRx immediate", "[safety]") {
  SafetyGate g;
  g.requestTx(0);
  g.confirmTx(100);
  const uint64_t d1 = g.txDeadlineMs();

  CHECK_FALSE(g.requestTx(200));                   // TX 中拒绝重新发起
  CHECK(g.txDeadlineMs() == d1);

  g.requestRx(300);
  CHECK(g.phase() == PttPhase::Rx);
  CHECK(g.txDeadlineMs() == 0);
}

TEST_CASE("safety: set-freq gate uses wide RX range", "[safety]") {
  // 设频（调谐）按接收范围门禁：广播/航空可收听
  CHECK(SafetyGate::authorizeSetFreq(7'100'000));
  CHECK(SafetyGate::authorizeSetFreq(98'000'000));
  CHECK(SafetyGate::authorizeSetFreq(121'500'000));
  CHECK_FALSE(SafetyGate::authorizeSetFreq(50'000));        // RX 下界外
  CHECK_FALSE(SafetyGate::authorizeSetFreq(2'400'000'000)); // RX 上界外
}

TEST_CASE("safety: TX gate locks to amateur whitelist", "[safety]") {
  CHECK(SafetyGate::authorizeTx(7'100'000));
  CHECK(SafetyGate::authorizeTx(145'000'000));
  CHECK_FALSE(SafetyGate::authorizeTx(98'000'000));   // 广播：可听不可发
  CHECK_FALSE(SafetyGate::authorizeTx(121'500'000));  // 航空：可听不可发
}
