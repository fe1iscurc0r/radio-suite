// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/frequency_keypad.h"

#include "app/touch_dialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace radio::app {
namespace {

// 从右往左每三位一个空格："145500000" → "145 500 000"
QString groupHz(const QString& digits) {
  QString out;
  const int n = digits.size();
  for (int i = 0; i < n; ++i) {
    if (i > 0 && (n - i) % 3 == 0) out.push_back(u' ');
    out.push_back(digits.at(i));
  }
  return out;
}

}  // namespace

FrequencyKeypad::FrequencyKeypad(QWidget* parent) : QDialog(parent) {
  setWindowTitle("输入频率");
  setMinimumWidth(300);
  auto* root = new QVBoxLayout(this);

  readout_ = new QLabel(this);
  readout_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  readout_->setMinimumHeight(40);
  readout_->setStyleSheet(
      "font-family: Consolas, monospace; font-size: 26px; color: #e8f0ff;"
      " background: #0d0f13; border: 1px solid #3a414c; padding: 2px 8px;");
  root->addWidget(readout_);

  mhz_ = new QLabel(this);
  mhz_->setAlignment(Qt::AlignRight);
  mhz_->setStyleSheet("color:#9fe;");
  root->addWidget(mhz_);

  auto* grid = new QGridLayout();
  static constexpr int kKeys[4][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}, {-1, 0, -2}};
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 3; ++c) {
      const int k = kKeys[r][c];
      QPushButton* b = nullptr;
      if (k >= 0) {
        b = new QPushButton(QString::number(k), this);
        connect(b, &QPushButton::clicked, this, [this, k] { pressDigit(k); });
      } else if (k == -1) {
        b = new QPushButton("C", this);
        connect(b, &QPushButton::clicked, this, &FrequencyKeypad::clearAll);
      } else {
        b = new QPushButton(QString::fromUtf8("⌫"), this);
        connect(b, &QPushButton::clicked, this, &FrequencyKeypad::backspace);
      }
      b->setMinimumSize(64, 48);
      grid->addWidget(b, r, c);
    }
  root->addLayout(grid);

  auto* row = new QHBoxLayout();
  auto* cancel = new QPushButton("取消", this);
  ok_btn_ = new QPushButton("确定", this);
  cancel->setMinimumSize(72, 44);
  ok_btn_->setMinimumSize(72, 44);
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  connect(ok_btn_, &QPushButton::clicked, this, &QDialog::accept);
  row->addWidget(cancel);
  row->addStretch(1);
  row->addWidget(ok_btn_);
  root->addLayout(row);

  touch::fitDialog(this);
  refresh();
}

void FrequencyKeypad::pressDigit(int d) {
  // 不接受前导 0：它只会白吃一位缓冲，把 145.500 挤成 14550000。
  if (digits_.isEmpty() && d == 0) return;
  // 满了就左移丢最高位，跟实机 VFO 一样：宁可丢头也不吞刚按下的那位。
  if (digits_.size() >= kMaxDigits) digits_.remove(0, 1);
  digits_.push_back(QChar(QLatin1Char('0' + d)));
  refresh();
}

void FrequencyKeypad::backspace() {
  digits_.chop(1);
  refresh();
}

void FrequencyKeypad::clearAll() {
  digits_.clear();
  refresh();
}

void FrequencyKeypad::setFrequencyHz(quint64 hz) {
  digits_ = hz > 0 ? QString::number(hz) : QString();
  if (digits_.size() > kMaxDigits) digits_ = digits_.right(kMaxDigits);
  refresh();
}

quint64 FrequencyKeypad::frequencyHz() const {
  return digits_.isEmpty() ? 0ULL : digits_.toULongLong();
}

QString FrequencyKeypad::displayText() const { return groupHz(digits_); }

void FrequencyKeypad::refresh() {
  const QString shown = groupHz(digits_);
  readout_->setText(shown.isEmpty() ? QStringLiteral("—") : shown);
  mhz_->setText(frequencyHz() == 0ULL
                    ? QStringLiteral("单位 Hz，如 145 500 000 = 145.500000 MHz")
                    : QStringLiteral("%1 MHz").arg(frequencyHz() / 1e6, 0, 'f', 6));
  ok_btn_->setEnabled(canAccept());
}

}  // namespace radio::app
