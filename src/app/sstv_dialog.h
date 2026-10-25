// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/sstv_dialog.h — SSTV 发送对话框（第一片：选图/预览/排期说明）
// 编码器（Robot36/Martin 时序 + VIS 码 + 生成 .wav）下一批落地：
// 时序参数须对表自校验（生成谱端点 1200/2300Hz 单测），不产错时序废品。
#pragma once

#include <QDialog>
#include <QImage>

class QLabel;
class QPushButton;

class SstvDialog : public QDialog {
  Q_OBJECT
 public:
  explicit SstvDialog(QWidget* parent = nullptr);

 private slots:
  void onPickImage();
  void onGenerate();  // Robot36 编码 → .wav（44100Hz 16bit PCM）

 private:
  QLabel* preview_ = nullptr;
  QLabel* info_ = nullptr;
  QPushButton* gen_btn_ = nullptr;
  QString image_path_;
  QImage img_;  // 320×240 RGB888
};
