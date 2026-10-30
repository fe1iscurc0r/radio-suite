// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/ft8_panel.h — FT8 本机解码台（不依赖 WSJT-X 进程）
//
// 吃 50003 下行 RX PCM，按 15 s UTC 时隙解调，结果进表格；双击一行把呼号/网格
// 与「刻度频率 + 音频频偏」送入主窗口。解码在 SlotDecoder 的工作线程里跑，
// UI 只做投递与展示。
#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <span>
#include <vector>

#include "ft8/ft8_slot.h"

class QLabel;
class QProgressBar;
class QPushButton;
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

  radio::ft8::SlotDecoder decoder_;
  quint64 dial_hz_ = 0;
  std::vector<RowMeta> rows_;

  QLabel* status_label_ = nullptr;
  QLabel* stats_label_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QPushButton* start_btn_ = nullptr;
  QPushButton* clear_btn_ = nullptr;
  QTableWidget* table_ = nullptr;
  QTimer* tick_ = nullptr;
};

}  // namespace radio::app
