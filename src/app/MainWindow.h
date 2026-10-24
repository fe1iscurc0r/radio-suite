// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/MainWindow.h — 主窗口（顶栏 / 调谐 / PTT / 瀑布）
#pragma once

#include <QComboBox>
#include <QLabel>
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
  void refreshState();    // RadioState → 标签
  void refreshPtt();      // SafetyGate 相位 → PTT 按钮文案
  void onBandClicked();   // 频段快捷
  void onTuneStep(int direction);
  void onBenchTick();     // 合成谱推进（M0 主线程；M2 移 dsp 线程）

 private:
  void buildUi();

  radio::app::RadioSession session_;

  WaterfallWidget* waterfall_ = nullptr;
  QLabel* freq_label_ = nullptr;
  QLabel* mode_label_ = nullptr;
  QLabel* smeter_label_ = nullptr;
  QLabel* status_label_ = nullptr;
  QPushButton* ptt_btn_ = nullptr;
  QComboBox* step_box_ = nullptr;

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
