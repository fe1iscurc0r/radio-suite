// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/voice_memory.h — Voice Memory（原厂件：语音记忆录放）
// 录音（AudioIn → wav 存 voice/）· 列表 · 播放（AudioOut）· 发射（PTT 门禁编排，
// 同 SSTV 模式）。对应原厂 RS-BA1 Voice Memory / Voice Memory Setting（chm 清单）。
#pragma once

#include <QDialog>
#include <QListWidget>
#include <memory>

class QPushButton;
class QLabel;

namespace radio::app {
class RadioSession;
}
namespace radio::audio {
class AudioIn;
class AudioOut;
}

class VoiceMemoryDialog : public QDialog {
  Q_OBJECT
 public:
  explicit VoiceMemoryDialog(radio::app::RadioSession* session,
                             QWidget* parent = nullptr);
  ~VoiceMemoryDialog() override;

 private slots:
  void onRecordToggle();
  void onPlay();
  void onTransmit();

 private:
  void refreshList();
  QString voiceDir() const;

  QListWidget* list_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* rec_btn_ = nullptr;
  QPushButton* play_btn_ = nullptr;
  QPushButton* tx_btn_ = nullptr;
  bool recording_ = false;
  bool tx_active_ = false;  // 后台播放中（PTT 已键，播完/析构回 RX）
  QString rec_path_;
  std::vector<int16_t> rec_pcm_;
  radio::app::RadioSession* session_ = nullptr;
  std::unique_ptr<radio::audio::AudioIn> audio_in_;
  std::unique_ptr<radio::audio::AudioOut> audio_out_;
};
