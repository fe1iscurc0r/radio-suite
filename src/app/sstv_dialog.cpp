// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/sstv_dialog.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/sstv.h"

SstvDialog::SstvDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("SSTV 发送");
  setMinimumSize(420, 380);
  auto* root = new QVBoxLayout(this);

  auto* pick = new QPushButton("选择图片（将降采样至 320×240 Robot36）", this);
  connect(pick, &QPushButton::clicked, this, &SstvDialog::onPickImage);
  root->addWidget(pick);

  preview_ = new QLabel("（未选图）", this);
  preview_->setAlignment(Qt::AlignCenter);
  preview_->setMinimumHeight(180);
  preview_->setStyleSheet("background:#10141c;");
  root->addWidget(preview_, 1);

  gen_btn_ = new QPushButton("生成 Robot36 .wav（36.8 秒）", this);
  gen_btn_->setEnabled(false);
  connect(gen_btn_, &QPushButton::clicked, this, &SstvDialog::onGenerate);
  root->addWidget(gen_btn_);

  info_ = new QLabel(
      "时序经 Goertzel 自校验（单测覆盖 VIS/sync/黑白扫描频率）。\n"
      "发射：用任何播放器播放 .wav 进电台 MOD（M2 接音频直出+PTT 联动）", this);
  info_->setStyleSheet("color:#9fe;");
  root->addWidget(info_);

  auto* close = new QPushButton("关闭", this);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  root->addWidget(close, 0, Qt::AlignRight);
}

void SstvDialog::onPickImage() {
  const QString path = QFileDialog::getOpenFileName(
      this, "选择图片", {}, "图片 (*.png *.jpg *.jpeg *.bmp)");
  if (path.isEmpty()) return;
  image_path_ = path;
  QImage img(path);
  if (img.isNull()) {
    preview_->setText("无法解码图片");
    gen_btn_->setEnabled(false);
    return;
  }
  img_ = img.scaled(320, 240, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
             .convertToFormat(QImage::Format_RGB888);
  preview_->setPixmap(QPixmap::fromImage(
      img_.scaled(320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
  gen_btn_->setEnabled(true);
  info_->setText(QString("已选：%1 → 点「生成 .wav」").arg(path.section('/', -1)));
}

void SstvDialog::onGenerate() {
  if (img_.isNull()) return;
  const QString out_path =
      image_path_.section('/', 0, -2) + "/" +
      image_path_.section('/', -1).section('.', 0, 0) + "_robot36.wav";
  QFile f(out_path);
  if (!f.open(QIODevice::WriteOnly)) {
    info_->setText("无法写 " + out_path);
    return;
  }
  std::vector<radio::core::sstv::Rgb> rgb(320 * 240);
  for (int y = 0; y < 240; ++y)
    for (int x = 0; x < 320; ++x) {
      const uchar* p = img_.scanLine(y) + x * 3;
      rgb[static_cast<size_t>(y) * 320 + x] = {p[0], p[1], p[2]};
    }
  const auto pcm = radio::core::sstv::encodeRobot36(rgb, 44100);
  // WAV 头（PCM 16bit 单声道）
  const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * 2);
  const uint32_t byte_rate = 44100 * 2;
  auto put32 = [&f](uint32_t v) {
    f.putChar(char(v & 0xFF)); f.putChar(char((v >> 8) & 0xFF));
    f.putChar(char((v >> 16) & 0xFF)); f.putChar(char((v >> 24) & 0xFF));
  };
  auto put16 = [&f](uint16_t v) {
    f.putChar(char(v & 0xFF)); f.putChar(char((v >> 8) & 0xFF));
  };
  f.write("RIFF"); put32(36 + data_bytes); f.write("WAVE");
  f.write("fmt "); put32(16); put16(1); put16(1);
  put32(44100); put32(byte_rate); put16(2); put16(16);
  f.write("data"); put32(data_bytes);
  f.write(reinterpret_cast<const char*>(pcm.data()),
          static_cast<qint64>(data_bytes));
  f.close();
  info_->setText(QString("已生成：%1（%2 KB）\n播放它进电台 MOD 即可发射")
                     .arg(out_path)
                     .arg(data_bytes / 1024));
}
