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

  // ---- 接收：从 .wav 解码（实时监听 M2 随音频链；先支持文件回环） ----
  auto* rx_head = new QLabel("接收（Robot36 解码）", this);
  root->addWidget(rx_head);
  auto* rx_row = new QHBoxLayout();
  auto* dec_btn = new QPushButton("从 .wav 解码…", this);
  connect(dec_btn, &QPushButton::clicked, this, &SstvDialog::onDecode);
  rx_row->addWidget(dec_btn);
  auto* save_btn = new QPushButton("保存图片…", this);
  connect(save_btn, &QPushButton::clicked, this, [this] {
    if (rx_img_.isNull()) return;
    const QString p = QFileDialog::getSaveFileName(this, "保存", "sstv_rx.png",
                                                   "PNG (*.png)");
    if (!p.isEmpty()) rx_img_.save(p);
  });
  rx_row->addWidget(save_btn);
  rx_row->addStretch(1);
  root->addLayout(rx_row);
  rx_view_ = new QLabel("（解码结果显示处）", this);
  rx_view_->setAlignment(Qt::AlignCenter);
  rx_view_->setMinimumHeight(160);
  rx_view_->setStyleSheet("background:#10141c;");
  root->addWidget(rx_view_, 1);
}

void SstvDialog::onDecode() {
  const QString path = QFileDialog::getOpenFileName(this, "选择 SSTV .wav",
                                                    {}, "WAV (*.wav)");
  if (path.isEmpty()) return;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    rx_view_->setText("无法打开文件");
    return;
  }
  const QByteArray all = f.readAll();
  f.close();
  // 简化 WAV 解析：44100/16bit/单声道，找 "data" 块
  int data_off = all.indexOf("data");
  if (data_off < 0) {
    rx_view_->setText("非 WAV 文件（无 data 块）");
    return;
  }
  const int32_t data_len =
      (static_cast<uint8_t>(all[data_off + 4])) |
      (static_cast<uint8_t>(all[data_off + 5]) << 8) |
      (static_cast<uint8_t>(all[data_off + 6]) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(all[data_off + 7])) << 24);
  const int n = data_len / 2;
  std::vector<int16_t> pcm(n);
  std::memcpy(pcm.data(), all.constData() + data_off + 8, data_len);

  const auto img = radio::core::sstv::decodeRobot36(pcm, 44100);
  if (img.empty()) {
    rx_view_->setText("未检出 Robot36 VIS 头（不是 SSTV 或时序不对）");
    return;
  }
  rx_img_ = QImage(320, 240, QImage::Format_RGB888);
  for (int y = 0; y < 240; ++y)
    for (int x = 0; x < 320; ++x) {
      const auto& p = img[static_cast<size_t>(y) * 320 + x];
      rx_img_.setPixel(x, y, qRgb(p.r, p.g, p.b));
    }
  rx_view_->setPixmap(QPixmap::fromImage(
      rx_img_.scaled(320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
  info_->setText(QString("解码完成：%1").arg(path.section('/', -1)));
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
