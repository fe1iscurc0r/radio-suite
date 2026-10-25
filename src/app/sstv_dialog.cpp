// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/sstv_dialog.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SstvDialog::SstvDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("SSTV 发送");
  setMinimumSize(420, 320);
  auto* root = new QVBoxLayout(this);

  auto* pick = new QPushButton("选择图片（将降采样至 320×240 Robot36）", this);
  connect(pick, &QPushButton::clicked, this, &SstvDialog::onPickImage);
  root->addWidget(pick);

  preview_ = new QLabel("（未选图）", this);
  preview_->setAlignment(Qt::AlignCenter);
  preview_->setMinimumHeight(180);
  preview_->setStyleSheet("background:#10141c;");
  root->addWidget(preview_, 1);

  info_ = new QLabel(
      "发送链路（按里程碑）：\n"
      "① 下一批：Robot36 编码器生成 .wav（时序对表自校验，不产废品）\n"
      "② M2：音频设备直出 + PTT 联动一键发射", this);
  info_->setStyleSheet("color:#9fe;");
  root->addWidget(info_);

  auto* close = new QPushButton("知道了", this);
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
    return;
  }
  // 预览按 Robot36 帧型（320×240）显示
  preview_->setPixmap(QPixmap::fromImage(img.scaled(
      320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
  info_->setText(QString("已选：%1\n① 下一批：Robot36 编码 .wav  ② M2：音频直出+PTT")
                     .arg(path.section('/', -1)));
}
