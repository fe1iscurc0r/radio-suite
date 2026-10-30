// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/sstv_dialog.h — SSTV 发送对话框（第一片：选图/预览/排期说明）
// 编码器（Robot36/Martin 时序 + VIS 码 + 生成 .wav）下一批落地：
// 时序参数须对表自校验（生成谱端点 1200/2300Hz 单测），不产错时序废品。
#pragma once

#include <QDialog>
#include <QImage>
#include <memory>

class QComboBox;
class QLabel;
class QPushButton;

namespace radio::app {
class RadioSession;
}
namespace radio::audio {
class AudioOut;
}

class SstvDialog : public QDialog {
  Q_OBJECT
 public:
  // 持主会话（裸指针+父窗口生命周期保证存活）：一键发射走 PTT 门禁编排
  SstvDialog(radio::app::RadioSession* session, QWidget* parent = nullptr);
  ~SstvDialog() override;  // unique_ptr<AudioOut> 需完整类型

 private slots:
  void onPickImage();
  void onGenerate();  // 按所选制式编码 → .wav
  void onDecode();    // 接收：从 .wav 自动识别制式解码 → 显示/保存
  void onTransmit();  // 一键发射：PTT 门禁→播放→回 RX

 private:
  QLabel* preview_ = nullptr;
  QLabel* info_ = nullptr;
  QPushButton* gen_btn_ = nullptr;
  QPushButton* tx_btn_ = nullptr;
  QComboBox* mode_box_ = nullptr;  // Robot36/72/Martin M1/M2（录音盒：制式自由）
  QLabel* rx_view_ = nullptr;      // 接收显示
  QString image_path_;
  QImage img_;      // 发送图（按制式缩放）
  QImage rx_img_;   // 接收解码结果
  std::vector<int16_t> last_pcm_;  // 最近编码结果（发射直接播，免回读文件）
  bool tx_active_ = false;         // 后台播放中（PTT 已键，播完/析构回 RX）
  radio::app::RadioSession* session_ = nullptr;
  std::unique_ptr<radio::audio::AudioOut> audio_;
};
