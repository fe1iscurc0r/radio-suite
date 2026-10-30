// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/audio_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

namespace radio::app {
namespace {

constexpr int kDefaultId = -1;
constexpr int kStatsIntervalMs = 500;

// 选中的 id 不在表里（设备被拔掉）时回落到"系统默认"
void selectId(QComboBox* combo, int id) {
  const int idx = combo->findData(id);
  combo->setCurrentIndex(idx >= 0 ? idx : combo->findData(kDefaultId));
}

void fillCombo(QComboBox* combo, const std::vector<AudioSettingsDialog::Device>& devs,
               int current) {
  combo->clear();
  combo->addItem("系统默认", kDefaultId);
  for (const auto& d : devs) combo->addItem(d.name, d.id);
  selectId(combo, current);
}

}  // namespace

AudioSettingsDialog::AudioSettingsDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("音频设置 · 50003 语音通路");
  setObjectName("audio_settings_dialog");
  setMinimumWidth(460);

  auto* root = new QVBoxLayout(this);
  auto* form = new QFormLayout();
  in_combo_ = new QComboBox(this);
  in_combo_->setObjectName("audio_in_combo");
  out_combo_ = new QComboBox(this);
  out_combo_->setObjectName("audio_out_combo");
  form->addRow("麦克风（PTT 上行）", in_combo_);
  form->addRow("扬声器（RX 下行）", out_combo_);
  root->addLayout(form);

  auto* box = new QGroupBox("链路质量（每 0.5 s 刷新）", this);
  auto* box_layout = new QVBoxLayout(box);
  stats_label_ = new QLabel(this);
  stats_label_->setObjectName("audio_stats_label");
  stats_label_->setWordWrap(true);
  stats_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  box_layout->addWidget(stats_label_);
  auto* hint = new QLabel(
      "丢/弃 = 重排窗判丢与迟到包；积压/欠载 = 扬声器队列；溢出 = 系统采集丢样。"
      "真机复验时以丢=0、欠载=0 为通过线。",
      this);
  hint->setWordWrap(true);
  hint->setStyleSheet("color:#8a939f; font-size:9pt;");
  box_layout->addWidget(hint);
  root->addWidget(box);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                       this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  timer_ = new QTimer(this);
  timer_->setInterval(kStatsIntervalMs);
  connect(timer_, &QTimer::timeout, this, &AudioSettingsDialog::refreshStats);
  refreshStats();
}

void AudioSettingsDialog::setDevices(const std::vector<Device>& inputs,
                                     const std::vector<Device>& outputs,
                                     int current_in, int current_out) {
  fillCombo(in_combo_, inputs, current_in);
  fillCombo(out_combo_, outputs, current_out);
}

void AudioSettingsDialog::setStatsProvider(std::function<AudioStats()> provider) {
  provider_ = std::move(provider);
  if (provider_) {
    timer_->start();
  } else {
    timer_->stop();
  }
  refreshStats();
}

int AudioSettingsDialog::chosenInputId() const {
  return in_combo_->currentData().toInt();
}

int AudioSettingsDialog::chosenOutputId() const {
  return out_combo_->currentData().toInt();
}

void AudioSettingsDialog::refreshStats() {
  if (!provider_) {
    stats_label_->setText("网络音频未开启：打开「网络音频」后显示下行/上行计数。");
    return;
  }
  stats_label_->setText(formatAudioStats(provider_()));
}

}  // namespace radio::app
