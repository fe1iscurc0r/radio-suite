// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/MainWindow.h — 主窗口（顶栏 / 调谐 / PTT / 瀑布）
#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>
#include <memory>
#include <vector>

#include "app/WaterfallWidget.h"
#include "app/session.h"
#include "dsp/fft.h"
#include "dsp/synth.h"
#include "dsp/waterfall_frame.h"

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(std::shared_ptr<radio::core::ICivTransport> transport,
                      QWidget* parent = nullptr);

 private slots:
  void refreshState();    // RadioState → 标签 + 模式/功能按钮回读同步
  void refreshPtt();      // SafetyGate 相位/剩余时间 → PTT 按钮文案
  void onBandClicked();   // 频段快捷
  void onTuneStep(int direction);
  void onTuneRejected(quint64 hz);  // RX 范围外拒绝 → 状态栏红字提示
  void onTxBlocked(quint64 hz);     // 非业余段拒绝发射
  void onManualFreq();              // 手动输频 GO
  void onBenchTick();     // 合成谱推进（M0 主线程；M2 移 dsp 线程）

 private:
  void buildUi();
  void flashStatus(const QString& text, bool warn);
  void syncTogglesFromState(const radio::core::RadioState& s);

  radio::app::RadioSession session_;

  WaterfallWidget* waterfall_ = nullptr;
  QLabel* freq_label_ = nullptr;
  QLabel* mode_label_ = nullptr;
  QLabel* smeter_label_ = nullptr;
  QLabel* status_label_ = nullptr;
  QPushButton* ptt_btn_ = nullptr;
  std::vector<QPushButton*> step_btns_;  // 排他 checkable 步进档（100Hz/1k/10k/100k）
  std::vector<QPushButton*> mode_btns_;  // 模式组（LSB/USB/AM/CW/FM/WFM，回读驱动）
  QPushButton* att_btn_ = nullptr;       // ATT 20dB（回读驱动）
  QPushButton* nb_btn_ = nullptr;        // NB（回读驱动）
  QPushButton* nr_btn_ = nullptr;        // NR（回读驱动）
  QPushButton* moni_btn_ = nullptr;      // MONI（回读驱动）
  QLineEdit* freq_edit_ = nullptr;       // 手动输频（MHz）
  QTimer status_timer_;                  // 状态栏红字自动恢复

  // 显示刷新：TX 倒计时/确认窗口剩余秒每 250ms 重画（QTimer 只做显示，判定在 core）
  QTimer ui_timer_;

  // 合成谱工作台（M0 spike：2048 点 FFT → 1024 显示列）
  static constexpr size_t kFftSize = 2048;
  QTimer bench_timer_;
  radio::dsp::SignalSynth synth_{kFftSize, 48000.0};
  radio::dsp::SpectrumAnalyzer analyzer_{kFftSize, 48000.0};
  radio::dsp::WaterfallProcessor wf_proc_{static_cast<size_t>(WaterfallWidget::kTexW)};
  std::vector<float> samples_;
  std::vector<float> col_db_;
  std::vector<uint8_t> row_bytes_;
};
