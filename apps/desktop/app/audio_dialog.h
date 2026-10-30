// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/audio_dialog.h — 音频设备选择 + 50003 链路质量读数
//
// 刻意不持有 NetworkAudio：设备表由调用方（主窗用 RtAudio 枚举）注入，
// 统计由 std::function 供数——这样 UI 能在离屏/无设备环境里完整测试。
#pragma once

#include <QDialog>
#include <QString>

#include <functional>
#include <vector>

#include "app/audio_stats.h"

class QComboBox;
class QLabel;
class QTimer;

namespace radio::app {

class AudioSettingsDialog : public QDialog {
  Q_OBJECT
 public:
  struct Device {
    int id = -1;  // -1 = 系统默认（跟随 Windows 默认设备切换）
    QString name;
  };

  explicit AudioSettingsDialog(QWidget* parent = nullptr);

  // 设备表（含"系统默认"项由对话框自己补），current_* 为当前选择（-1=默认）
  void setDevices(const std::vector<Device>& inputs,
                  const std::vector<Device>& outputs, int current_in,
                  int current_out);
  // 空 function = 网络音频未开启（只显示提示，不显示计数）
  void setStatsProvider(std::function<AudioStats()> provider);

  int chosenInputId() const;
  int chosenOutputId() const;

 public slots:
  void refreshStats();

 private:
  QComboBox* in_combo_ = nullptr;
  QComboBox* out_combo_ = nullptr;
  QLabel* stats_label_ = nullptr;
  QTimer* timer_ = nullptr;
  std::function<AudioStats()> provider_;
};

}  // namespace radio::app
