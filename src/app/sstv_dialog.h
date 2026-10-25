// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/sstv_dialog.h — SSTV 发送对话框（第一片：选图/预览/排期说明）
// 编码器（Robot36/Martin 时序 + VIS 码 + 生成 .wav）下一批落地：
// 时序参数须对表自校验（生成谱端点 1200/2300Hz 单测），不产错时序废品。
#pragma once

#include <QDialog>
#include <QImage>

class QComboBox;
class QLabel;
class QPushButton;

class SstvDialog : public QDialog {
  Q_OBJECT
 public:
  explicit SstvDialog(QWidget* parent = nullptr);

 private slots:
  void onPickImage();
  void onGenerate();  // 按所选制式编码 → .wav
  void onDecode();    // 接收：从 .wav 自动识别制式解码 → 显示/保存

 private:
  QLabel* preview_ = nullptr;
  QLabel* info_ = nullptr;
  QPushButton* gen_btn_ = nullptr;
  QComboBox* mode_box_ = nullptr;  // Robot36/72/Martin M1/M2（录音盒：制式自由）
  QLabel* rx_view_ = nullptr;      // 接收显示
  QString image_path_;
  QImage img_;      // 发送图（按制式缩放）
  QImage rx_img_;   // 接收解码结果
};
