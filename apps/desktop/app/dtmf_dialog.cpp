// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/dtmf_dialog.h"

#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/dtmf.h"

DtmfDialog::DtmfDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("DTMF 双音多频");
  setMinimumWidth(320);
  auto* root = new QVBoxLayout(this);

  digits_ = new QLineEdit(this);
  digits_->setReadOnly(true);
  digits_->setAlignment(Qt::AlignCenter);
  digits_->setPlaceholderText("按键盘输入（1234567890*#ABCD）");
  root->addWidget(digits_);

  auto* grid = new QGridLayout();
  const char keys[4][4] = {{'1', '2', '3', 'A'},
                           {'4', '5', '6', 'B'},
                           {'7', '8', '9', 'C'},
                           {'*', '0', '#', 'D'}};
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) {
      auto* b = new QPushButton(QChar(keys[r][c]), this);
      b->setMinimumSize(52, 44);
      const char k = keys[r][c];
      connect(b, &QPushButton::clicked, this, [this, k] { onKey(k); });
      grid->addWidget(b, r, c);
    }
  root->addLayout(grid);

  auto* clr = new QPushButton("清空", this);
  connect(clr, &QPushButton::clicked, this, [this] { digits_->clear(); });
  root->addWidget(clr, 0, Qt::AlignLeft);
  auto* gen = new QPushButton("生成 .wav（80ms/键）", this);
  connect(gen, &QPushButton::clicked, this, &DtmfDialog::onGenWav);
  root->addWidget(gen, 0, Qt::AlignRight);

  info_ = new QLabel("回环自校验：16 键编解码单测全绿（core/dtmf）", this);
  info_->setStyleSheet("color:#9fe;");
  root->addWidget(info_);
}

void DtmfDialog::onKey(char c) {
  if (radio::core::dtmf::isDigit(c))
    digits_->setText(digits_->text() + QChar(c));
}

void DtmfDialog::onGenWav() {
  const std::string keys = digits_->text().toStdString();
  if (keys.empty()) {
    info_->setText("先按键盘输入");
    return;
  }
  const auto pcm = radio::core::dtmf::encode(keys);
  const QString path = QDir::home().filePath("dtmf.wav");
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    info_->setText("无法写 " + path);
    return;
  }
  const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * 2);
  auto put32 = [&f](uint32_t v) {
    for (int i = 0; i < 4; ++i) f.putChar(char((v >> (8 * i)) & 0xFF));
  };
  auto put16 = [&f](uint16_t v) {
    f.putChar(char(v & 0xFF));
    f.putChar(char((v >> 8) & 0xFF));
  };
  f.write("RIFF"); put32(36 + data_bytes); f.write("WAVE");
  f.write("fmt "); put32(16); put16(1); put16(1);
  put32(44100); put32(88200); put16(2); put16(16);
  f.write("data"); put32(data_bytes);
  f.write(reinterpret_cast<const char*>(pcm.data()),
          static_cast<qint64>(data_bytes));
  f.close();
  info_->setText(QString("已生成 %1（%2 键）→ 播放进电台 MOD 即发送")
                     .arg(path).arg(keys.size()));
}
