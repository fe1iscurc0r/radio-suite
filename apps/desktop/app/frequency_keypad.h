// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/frequency_keypad.h — 手台式数字键盘输频
#pragma once

#include <QDialog>
#include <QString>

#include <cstdint>

class QLabel;

namespace radio::app {

// 为什么不用 QLineEdit：手机上点输入框弹的是系统软键盘，横屏下它会盖住频率区，
// 而调频是最高频的动作——看不见当前频率就是盲调。这里自己画 12 键，
// 缓冲按 Hz 左移进位（按 1 4 5 5 0 0 0 0 0 = 145.500000 MHz），与实机 VFO 一致。
class FrequencyKeypad : public QDialog {
  Q_OBJECT
 public:
  explicit FrequencyKeypad(QWidget* parent = nullptr);

  // 缓冲内容按 Hz 解释；一位没按返回 0。
  quint64 frequencyHz() const;
  // 用现值回填缓冲；0 视为空。
  void setFrequencyHz(quint64 hz);
  // 分组后的读数，形如 "145 500 000"；空缓冲返回空串。
  QString displayText() const;
  // 空缓冲时「确定」不可用，避免一次误触把频率清成 0。
  bool canAccept() const { return !digits_.isEmpty(); }

 private:
  void pressDigit(int d);
  void backspace();
  void clearAll();
  void refresh();

  static constexpr int kMaxDigits = 11;  // 上限 10 GHz，11 位够铺
  QString digits_;
  QLabel* readout_ = nullptr;
  QLabel* mhz_ = nullptr;
  class QPushButton* ok_btn_ = nullptr;
};

}  // namespace radio::app
