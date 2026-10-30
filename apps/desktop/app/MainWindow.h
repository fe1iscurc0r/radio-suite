// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/MainWindow.h — 主窗口（顶栏 / 调谐 / PTT / 瀑布）
#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>
#include <cstdint>
#include <memory>
#include <vector>
class QHBoxLayout;
class QVBoxLayout;
class QCloseEvent;

#include "app/WaterfallWidget.h"
#include "app/session.h"
#ifdef RADIO_SUITE_HAVE_LOGBOOK
#include "logbook/logbook.h"
#endif
#include "dsp/fft.h"
#include "dsp/synth.h"
#include "dsp/waterfall_frame.h"

class QListWidget;
namespace radio::app {
class AprsPanel;
class Ft8Panel;
class WsjtxPanel;
class CwKeyerDialog;
class SetModeDialog;
class MemoryChannelDialog;
class NetworkAudio;
}
namespace radio::audio {
class AudioOut;
}
class QLineEdit;
class QPushButton;
class SMeterWidget;
class PoSwrWidget;
class DialWidget;
class QComboBox;

class MainWindow : public QMainWindow, public std::enable_shared_from_this<MainWindow> {
  Q_OBJECT
 public:
  explicit MainWindow(std::shared_ptr<radio::core::ICivTransport> transport,
                      QWidget* parent = nullptr);
  ~MainWindow() override;

 protected:
  // 副面板是无父顶层窗口：主窗关闭时要一起收掉，否则进程不会退出
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void refreshState();    // RadioState → 标签 + 模式/功能按钮回读同步
  void refreshPtt();      // SafetyGate 相位/剩余时间 → PTT 按钮文案
  void onBandClicked();   // 频段快捷
  void onTuneStep(int direction);
  void onTuneRejected(quint64 hz);  // RX 范围外拒绝 → 状态栏红字提示
  void onTxBlocked(quint64 hz);     // 非业余段拒绝发射
  void onManualFreq();              // 手动输频 GO
  void onBenchTick();     // 合成谱推进（M0 主线程；M2 移 dsp 线程）
  void onMemSave();       // 本地记忆：当前频/模存入所选槽
  void onMemRecall();     // 本地记忆：取出槽内频/模
  void onAudioSettings();  // 音频设备选择 + 50003 链路质量读数
#ifdef RADIO_SUITE_HAVE_LOGBOOK
  void onLogQso();        // 记一条 QSO（自动填频率/模式/UTC，写 HamLog 库）
  void onExportAdif();    // 导出 ADIF（radio-suite 与 hamlog 的交换契约）
#endif

 private:
  void buildUi();
  QVBoxLayout* buildTuneBlock(QWidget* central);  // 旋钮调谐台 + 频段 + 双 VFO
  QVBoxLayout* buildFuncBlock(QWidget* central);  // 模式/中继 + 功能开关两行
  void flashStatus(const QString& text, bool warn);
  void syncTogglesFromState(const radio::core::RadioState& s);

  radio::app::RadioSession session_;
  radio::app::RadioSession* session_alias_ = nullptr;  // 指向 session_（对话框用，无所有权）

  WaterfallWidget* waterfall_ = nullptr;
  QLabel* freq_label_ = nullptr;
  QLabel* mode_label_ = nullptr;
  SMeterWidget* smeter_ = nullptr;      // 半圆指针 S 表（主信息）
  PoSwrWidget* poswr_ = nullptr;        // Po/SWR 双条表（主信息）
  QLabel* status_label_ = nullptr;
  QPushButton* ptt_btn_ = nullptr;
  QPushButton* aprs_big_btn_ = nullptr; // 右侧大按键（显眼件）
  QPushButton* wsjtx_big_btn_ = nullptr;  // WSJT-X UDP 接入
  QPushButton* ft8_big_btn_ = nullptr;    // FT8 本机解码（不经 WSJT-X）
  QPushButton* sstv_big_btn_ = nullptr;
  QPushButton* rit_btn_ = nullptr;      // RIT / ΔTX / Split（原厂件，回读驱动）
  QPushButton* dtx_btn_ = nullptr;
  QPushButton* split_btn_ = nullptr;
  DialWidget* dial_ = nullptr;          // 主调谐旋钮（原厂 M-Tune 件）
  QLabel* step_label_ = nullptr;        // 当前旋钮步进档
  QPushButton* repeater_btn_ = nullptr; // 中继/亚音对话框入口（回读驱动文案）
  QComboBox* mem_slot_ = nullptr;       // 本地记忆槽（电台内存信道 M3 接）
  QLabel* tx_label_ = nullptr;          // 中继双频副显示（DUP± 时 TX=RX∓/±offset）
  QLabel* sub_label_ = nullptr;         // SUB 带（另一 VFO）频率/模式
  QPushButton* vfo_a_btn_ = nullptr;    // 选 MAIN
  QPushButton* vfo_b_btn_ = nullptr;    // 选 SUB
  QPushButton* vfo_swap_btn_ = nullptr; // A/B 交换
  QPushButton* vfo_equal_btn_ = nullptr;  // A=B
  QPushButton* dtmf_big_btn_ = nullptr;
  std::vector<QPushButton*> step_btns_;  // 排他 checkable 步进档（100Hz/1k/10k/100k）
  std::vector<QPushButton*> mode_btns_;  // 模式组（LSB/USB/AM/CW/FM/WFM，回读驱动）
  QPushButton* att_btn_ = nullptr;       // ATT 20dB（回读驱动）
  QPushButton* nb_btn_ = nullptr;        // NB（回读驱动）
  QPushButton* nr_btn_ = nullptr;        // NR（回读驱动）
  QPushButton* moni_btn_ = nullptr;      // MONI（回读驱动）
  QPushButton* notch_btn_ = nullptr;     // Auto Notch（回读驱动）
  QPushButton* vox_btn_ = nullptr;       // VOX（回读驱动）
  QPushButton* pamp_btn_ = nullptr;      // PAMP 循环 OFF/P1/P2（回读驱动）
  QPushButton* agc_btn_ = nullptr;       // AGC 循环 F/M/S（回读驱动）
  QPushButton* dup_btn_ = nullptr;       // Dup 循环 SPLX/DUP-/DUP+（回读驱动）
  QLineEdit* freq_edit_ = nullptr;       // 手动输频（MHz）
  QPushButton* audio_btn_ = nullptr;     // 50003 网络音频开关（mock 下禁用）
  QPushButton* audio_setup_btn_ = nullptr;  // 音频设备/诊断

  // 50003 音频链（created after buildUi；销毁顺序在 session_ 之前）
  std::unique_ptr<radio::app::NetworkAudio> network_audio_;
  std::vector<int16_t> rx_ring_;         // 最近 RX PCM（频谱源）
  size_t rx_ring_pos_ = 0;
  bool rx_have_ = false;

  // hamlog / APRS / WSJT-X 融合区
  radio::app::AprsPanel* aprs_panel_ = nullptr;
  radio::app::WsjtxPanel* wsjtx_panel_ = nullptr;
  radio::app::Ft8Panel* ft8_panel_ = nullptr;
  // FT8 整槽发射的出口（与 SSTV 一键发射同路：PTT 门禁 + 本机声卡直出）
  std::unique_ptr<radio::audio::AudioOut> ft8_tx_audio_;
  QLineEdit* qso_call_edit_ = nullptr;   // QSO 对方呼号
  QListWidget* qso_list_ = nullptr;      // 最近 QSO（HamLog 库实时视图）
#ifdef RADIO_SUITE_HAVE_LOGBOOK
  radio::logbook::HamLogDb hamlog_;
  void refreshQsoList();
#endif
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
