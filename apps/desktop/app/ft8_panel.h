// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/ft8_panel.h — FT8 本机解码台（不依赖 WSJT-X 进程）
//
// 吃 50003 下行 RX PCM，按 15 s UTC 时隙解调，结果进表格；双击一行把呼号/网格
// 与「刻度频率 + 音频频偏」送入主窗口。解码在 SlotDecoder 的工作线程里跑，
// UI 只做投递与展示。发射侧只做排期与文案：算出下一个可用槽起点，到点把整槽
// GFSK PCM 交给主窗注入的出口（PTT 门禁与播放都在那边）。
#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "ft8/ft8_slot.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace radio::app {

class Ft8Panel : public QWidget {
  Q_OBJECT
 public:
  explicit Ft8Panel(QWidget* parent = nullptr);
  ~Ft8Panel() override;

  bool decoding() const;
  // utc_ms <= 0 用当前时刻；测试传 0 可立刻从边界开始
  void startDecoding(int64_t utc_ms = -1);
  void stopDecoding();

  // 主窗刻度频率：双击回填的目标频率 = 刻度 + 音频频偏
  void setDialHz(quint64 hz) { dial_hz_ = hz; }
  quint64 dialHz() const { return dial_hz_; }

  // 50003 RX PCM（48k s16 mono），本地关闭时直接丢弃
  void feedRxPcm(std::span<const int16_t> pcm);

  // 发射出口：交 48k s16 mono 一整个时隙的 PCM，返回 false = 门禁拒绝/设备不可用
  // （由主窗注入：beginScriptedTx → playAsync → 播完 endScriptedTx）
  void setTxController(std::function<bool(std::vector<int16_t>, uint32_t)> tx) {
    tx_ = std::move(tx);
  }
  // 排期一次发射：等到下一个满足 parity 的槽起点再播。now_ms 可注入便于测试。
  // 返回 false = 文本无法按 FT8 打包，或已有一次发射在等待
  bool scheduleTransmit(const QString& text, float f0_hz, int parity,
                        int64_t now_ms);
  bool txScheduled() const { return tx_pending_; }
  int64_t txStartUtcMs() const { return tx_start_ms_; }
  void cancelTransmit();
  // 到点触发（now_ms <= 0 用当前时刻；测试可传值绕开定时器）
  bool fireTransmit(int64_t now_ms = -1);

  // 外部/测试注入一槽解码结果（避免测试里等 15 s 真实时隙）
  void appendResult(const radio::ft8::SlotResult& result);
  int decodeRowCount() const;
  QString decodeText(int row) const;
  bool activateDecode(int row);

 signals:
  void statusMessage(const QString& text, bool error);
  void decodeActivated(const QString& call, const QString& grid,
                       quint64 frequency_hz, const QString& mode);

 private:
  struct RowMeta {
    QString text;
    double df_hz = 0.0;
  };

  void buildUi();
  void refreshTick();
  void clearRows();
  void onTxClicked();
  void updateTxLabel(int64_t now_ms);

  radio::ft8::SlotDecoder decoder_;
  quint64 dial_hz_ = 0;
  std::vector<RowMeta> rows_;

  std::function<bool(std::vector<int16_t>, uint32_t)> tx_;
  std::vector<int16_t> tx_pcm_;
  int64_t tx_start_ms_ = 0;
  bool tx_pending_ = false;
  QTimer* tx_timer_ = nullptr;

  QLabel* status_label_ = nullptr;
  QLabel* stats_label_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QPushButton* start_btn_ = nullptr;
  QPushButton* clear_btn_ = nullptr;
  QPushButton* tx_btn_ = nullptr;
  QLineEdit* msg_edit_ = nullptr;
  QSpinBox* f0_spin_ = nullptr;
  QComboBox* parity_box_ = nullptr;
  QLabel* tx_info_ = nullptr;
  QTableWidget* table_ = nullptr;
  QTimer* tick_ = nullptr;
};

}  // namespace radio::app
