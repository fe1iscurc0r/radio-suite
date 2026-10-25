// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · apps/spike/engine_spike.h — WO-274 B 节 spike 客户端
// 连 rigplane sidecar：/api/v1/scope 二进制帧 → 瀑布；/api/v1/ws 命令；
// /api/v1/audio PCM16 → 音频计数。四硬指标的测量点全部内建（真机开机即测）。
#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWebSocket>
#include <QWidget>
#include <cstdint>

class QLabel;
class QPushButton;
class QVBoxLayout;
class WaterfallWidget;

namespace radio::app {
class RadioSession;  // 仅瀑布控件复用；spike 不走自研协议
}

namespace radio::spike {

// rigplane scope wire（web/protocol.py:56-79，docs/engine/rigplane-api-notes.md §2.1）
struct ScopeWireFrame {
  uint8_t receiver = 0;
  uint8_t mode = 0;
  uint32_t start_freq_hz = 0;
  uint32_t end_freq_hz = 0;
  uint16_t sequence = 0;
  bool out_of_range = false;
  std::vector<uint8_t> pixels;
};

class EngineSpike : public QWidget {
  Q_OBJECT
 public:
  explicit EngineSpike(const QString& sidecar_host, QWidget* parent = nullptr);

  // 16B 头解码（合成帧单测也用）
  static bool decodeScopeFrame(const QByteArray& binary, ScopeWireFrame* out);

 private slots:
  void onScopeConnected();
  void onScopeBinary(const QByteArray& msg);
  void onWsConnected();
  void onWsText(const QString& msg);
  void onAudioBinary(const QByteArray& msg);
  void reconnectAll();
  void sendPing();       // 15s keepalive（对齐 rigplane 前端）
  void onTuneClicked();
  void onPttClicked();

 private:
  void armReconnect();   // 5s 重连（指标 4）
  QString wsUrl(const char* path) const;

  QWebSocket scope_;
  QWebSocket ws_;
  QWebSocket audio_;
  WaterfallWidget* waterfall_ = nullptr;
  QLabel* metrics_ = nullptr;
  QLabel* status_ = nullptr;
  QString host_;
  QTimer reconnect_timer_;
  QTimer ping_timer_;
  QElapsedTimer last_scope_frame_;
  // 指标
  int fps_count_ = 0;
  qint64 fps_window_ms_ = 0;
  double last_fps_ = 0.0;
  double last_frame_gap_ms_ = 0.0;
  qint64 ptt_sent_ms_ = 0;
  double last_ptt_ms_ = -1.0;
  uint32_t audio_frames_ = 0;
  uint16_t last_audio_seq_ = 0;
  uint32_t audio_dropped_ = 0;
  bool reconnect_armed_ = false;
};

}  // namespace radio::spike
