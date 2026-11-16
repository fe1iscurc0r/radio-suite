// SPDX-License-Identifier: AGPL-3.0-or-later
// 数字键盘输频。它存在的理由：手机上点 QLineEdit 会弹系统软键盘，横屏下键盘
// 盖住频率区，而调频是最高频的动作——看不见当前频率就是盲调。
// 这里锁的是「按出来的是 Hz、左移进位、空缓冲不许确定」这三件事。
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QPushButton>
#include <QString>

#include "app/frequency_keypad.h"

namespace {

QApplication& app() {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  static int argc = 1;
  static char name[] = "radio-ui-tests";
  static char* argv[] = {name, nullptr};
  static QApplication instance(argc, argv);
  return instance;
}

QPushButton* keyWithText(QWidget* root, const QString& text) {
  for (auto* b : root->findChildren<QPushButton*>())
    if (b->text() == text) return b;
  return nullptr;
}

void typeDigits(radio::app::FrequencyKeypad& pad, const QString& seq) {
  for (const QChar c : seq) {
    auto* key = keyWithText(&pad, QString(c));
    REQUIRE(key != nullptr);
    key->click();
  }
}

}  // namespace

TEST_CASE("frequency keypad: digits are Hz and group in threes", "[freq][keypad]") {
  app();
  radio::app::FrequencyKeypad pad;
  typeDigits(pad, "145500000");
  REQUIRE(pad.frequencyHz() == 145500000ULL);
  REQUIRE(pad.displayText() == QStringLiteral("145 500 000"));
}

TEST_CASE("frequency keypad: leading zero is dropped, C and backspace edit",
          "[freq][keypad]") {
  app();
  radio::app::FrequencyKeypad pad;
  typeDigits(pad, "0");
  REQUIRE(pad.frequencyHz() == 0ULL);
  REQUIRE_FALSE(pad.canAccept());

  typeDigits(pad, "5000");
  REQUIRE(pad.frequencyHz() == 5000ULL);
  REQUIRE(pad.displayText() == QStringLiteral("5 000"));

  auto* back = keyWithText(&pad, QString::fromUtf8("⌫"));
  REQUIRE(back != nullptr);
  back->click();
  REQUIRE(pad.frequencyHz() == 500ULL);

  auto* clr = keyWithText(&pad, QStringLiteral("C"));
  REQUIRE(clr != nullptr);
  clr->click();
  REQUIRE_FALSE(pad.canAccept());
  REQUIRE(pad.displayText().isEmpty());
}

TEST_CASE("frequency keypad: overflow shifts left instead of eating the newest digit",
          "[freq][keypad]") {
  app();
  radio::app::FrequencyKeypad pad;
  typeDigits(pad, "123456789012");
  // 11 位封顶：最新的 2 必须在，最老的 1 被挤掉，而不是反过来把 2 吞了。
  REQUIRE(pad.frequencyHz() == 23456789012ULL);
}

TEST_CASE("frequency keypad: seeding from the live frequency round-trips",
          "[freq][keypad]") {
  app();
  radio::app::FrequencyKeypad pad;
  pad.setFrequencyHz(433500000ULL);
  REQUIRE(pad.frequencyHz() == 433500000ULL);
  REQUIRE(pad.displayText() == QStringLiteral("433 500 000"));
  REQUIRE(pad.canAccept());

  pad.setFrequencyHz(0ULL);
  REQUIRE_FALSE(pad.canAccept());
  REQUIRE(pad.frequencyHz() == 0ULL);
}

TEST_CASE("frequency keypad: OK stays disabled until something is entered",
          "[freq][keypad]") {
  app();
  radio::app::FrequencyKeypad pad;
  auto* ok = keyWithText(&pad, QStringLiteral("确定"));
  REQUIRE(ok != nullptr);
  REQUIRE_FALSE(ok->isEnabled());
  typeDigits(pad, "1");
  REQUIRE(ok->isEnabled());
}
