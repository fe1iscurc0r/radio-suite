// SPDX-License-Identifier: AGPL-3.0-or-later
// spike 客户端实现——wire 格式出处见 rigplane-api-notes.md §2（源码级核对）
#include "apps/spike/engine_spike.h"

#include <QDateTime>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "WaterfallWidget.h"

namespace radio::spike {

EngineSpike::EngineSpike(const QString& sidecar_host, QWidget* parent)
    : QWidget(parent), host_(sidecar_host) {
  auto* root = new QVBoxLayout(this);
  status_ = new QLabel("connecting sidecar…", this);
  root->addWidget(status_);
  metrics_ = new QLabel("metrics: —", this);
  root->addWidget(metrics_);

  auto* row = new QHBoxLayout();
  auto* tune = new QPushButton("TUNE 14.100", this);
  connect(tune, &QPushButton::clicked, this, &EngineSpike::onTuneClicked);
  row->addWidget(tune);
  auto* ptt = new QPushButton("PTT", this);
  connect(ptt, &QPushButton::clicked, this, &EngineSpike::onPttClicked);
  row->addWidget(ptt);
  row->addStretch(1);
  root->addLayout(row);

  waterfall_ = new WaterfallWidget(this);
  root->addWidget(waterfall_, 1);

  const auto open = [this](QWebSocket& s, const char* path) {
    connect(&s, &QWebSocket::connected, this,
            [this, path] {
              if (qstrcmp(path, "/api/v1/scope") == 0) onScopeConnected();
              else if (qstrcmp(path, "/api/v1/ws") == 0) onWsConnected();
            });
    connect(&s, &QWebSocket::disconnected, this, &EngineSpike::armReconnect);
    connect(&s, &QWebSocket::errorOccurred, this, &EngineSpike::armReconnect);
  };
  open(scope_, "/api/v1/scope");
  open(ws_, "/api/v1/ws");
  open(audio_, "/api/v1/audio");
  connect(&scope_, &QWebSocket::binaryMessageReceived, this,
          &EngineSpike::onScopeBinary);
  connect(&ws_, &QWebSocket::textMessageReceived, this, &EngineSpike::onWsText);
  connect(&audio_, &QWebSocket::binaryMessageReceived, this,
          &EngineSpike::onAudioBinary);

  connect(&reconnect_timer_, &QTimer::timeout, this, &EngineSpike::reconnectAll);
  connect(&ping_timer_, &QTimer::timeout, this, &EngineSpike::sendPing);
  ping_timer_.start(15000);
  reconnectAll();
}

QString EngineSpike::wsUrl(const char* path) const {
  return QString("ws://%1:8490%2").arg(host_, path);
}

void EngineSpike::armReconnect() {
  if (reconnect_armed_) return;
  reconnect_armed_ = true;
  status_->setText("sidecar 断开——5s 后重连（指标 4：错误态已进入）");
  reconnect_timer_.singleShot(5000, this, &EngineSpike::reconnectAll);
}

void EngineSpike::reconnectAll() {
  reconnect_armed_ = false;
  status_->setText("connecting…");
  scope_.open(wsUrl("/api/v1/scope"));
  ws_.open(wsUrl("/api/v1/ws"));
  audio_.open(wsUrl("/api/v1/audio"));
}

void EngineSpike::sendPing() {
  const auto ping = QByteArrayLiteral("{\"type\":\"ping\"}");
  if (scope_.isValid()) scope_.sendTextMessage(ping);
  if (ws_.isValid()) ws_.sendTextMessage(ping);
}

void EngineSpike::onScopeConnected() {
  last_scope_frame_.start();
  fps_count_ = 0;
  fps_window_ms_ = 0;
}

void EngineSpike::onScopeBinary(const QByteArray& msg) {
  ScopeWireFrame f;
  if (!decodeScopeFrame(msg, &f)) return;
  // 指标 1：帧间隔与 FPS
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (last_scope_frame_.isValid()) {
    last_frame_gap_ms_ = static_cast<double>(last_scope_frame_.elapsed());
    fps_count_ += 1;
    fps_window_ms_ += static_cast<qint64>(last_frame_gap_ms_);
    if (fps_window_ms_ >= 1000) {
      last_fps_ = fps_count_ * 1000.0 / fps_window_ms_;
      fps_count_ = 0;
      fps_window_ms_ = 0;
    }
  }
  last_scope_frame_.restart();
  // 瀑布：把 pixels 直接推显示（1024 列映射，复用 M0 瀑布控件）
  static std::vector<uint8_t> row(WaterfallWidget::kTexW, 0);
  row.assign(f.pixels.begin(), f.pixels.end());
  row.resize(WaterfallWidget::kTexW, row.empty() ? 0 : row.back());
  waterfall_->pushRow(row.data(), WaterfallWidget::kTexW);

  metrics_->setText(
      QString("FPS %1 | gap %2ms | pixels %3 | seq %4 | PTT %5ms | "
              "audio %6 pkts (drop %7)")
          .arg(last_fps_, 0, 'f', 1)
          .arg(last_frame_gap_ms_, 0, 'f', 0)
          .arg(f.pixels.size())
          .arg(f.sequence)
          .arg(last_ptt_ms_, 0, 'f', 0)
          .arg(audio_frames_)
          .arg(audio_dropped_));
}

void EngineSpike::onWsConnected() {
  // 订阅状态流（cmd envelope；rigplane 首条 /ws 为 primary）
  ws_.sendTextMessage(QString::fromUtf8(
      makeCommandEnvelope("sub1", "subscribe", QByteArrayLiteral("{}"))));
}

void EngineSpike::onWsText(const QString& msg) {
  // 指标 3：PTT lifecycle 到达时间
  if (ptt_sent_ms_ > 0 && msg.contains("command_lifecycle") &&
      msg.contains("ptt")) {
    last_ptt_ms_ = static_cast<double>(QDateTime::currentMSecsSinceEpoch() -
                                       ptt_sent_ms_);
    ptt_sent_ms_ = 0;
  }
  if (msg.contains("\"type\":\"pong\"")) return;
}

void EngineSpike::onAudioBinary(const QByteArray& msg) {
  if (msg.size() < 8) return;
  const auto* d = reinterpret_cast<const uint8_t*>(msg.constData());
  if (d[0] != 0x10) return;  // RX only
  const uint16_t seq = uint16_t(d[2]) | (uint16_t(d[3]) << 8);
  if (last_audio_seq_ != 0) {
    const uint16_t expect = uint16_t(last_audio_seq_ + 1);
    if (seq != expect) audio_dropped_ += (seq > expect) ? (seq - expect) : 1;
  }
  last_audio_seq_ = seq;
  audio_frames_ += 1;
}

void EngineSpike::onTuneClicked() {
  if (!ws_.isValid()) return;
  ws_.sendTextMessage(QString::fromUtf8(makeCommandEnvelope(
      "t1", "set_freq", QByteArrayLiteral("{\"freq\":14100000,\"receiver\":0}"))));
}

void EngineSpike::onPttClicked() {
  if (!ws_.isValid()) return;
  ptt_sent_ms_ = QDateTime::currentMSecsSinceEpoch();
  ws_.sendTextMessage(QString::fromUtf8(
      makeCommandEnvelope("p1", "ptt", QByteArrayLiteral("{\"state\":true}"))));
  // 5 秒后自动回 RX（对齐本仓 SafetyGate 语义）
  QTimer::singleShot(5000, this, [this] {
    ws_.sendTextMessage(QString::fromUtf8(
        makeCommandEnvelope("p2", "ptt", QByteArrayLiteral("{\"state\":false}"))));
  });
}

}  // namespace radio::spike
