// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · bus/bus_server.h — 本机 HTTP 总线（scratchpad/lumo 集成契约，M5 第一片）
//
// 工单 M5：GET /api/state、GET /api/spectrum、POST /api/control；
// 默认 bind 127.0.0.1，非环回必须 --token（第一片即实现该安全要求）。
// 频谱流第一片用快照轮询（M5 全量再上 SSE/chunked）；总线自持合成谱源
// （GUI 各自一份，M2 真音频时统一为 dsp 线程单源）。
#pragma once

#include <QHttpServer>
#include <QHttpServerResponder>
#include <QObject>
#include <QTimer>
#include <QJsonArray>
#include <vector>

#include "session/session.h"
#include "dsp/fft.h"
#include "dsp/synth.h"

namespace radio::bus {

class BusServer : public QObject {
  Q_OBJECT
 public:
  struct Config {
    uint16_t port = 8765;
    QString token;               // 非空时校验 ?token= 或 X-Radio-Token
    QString bind_addr = "127.0.0.1";
  };

  BusServer(radio::app::RadioSession& session, QObject* parent = nullptr);

  bool start(const Config& cfg, QString* error);
  uint64_t spectrumSeq() const { return analyzer_.frameSeq(); }
  uint16_t port() const { return bound_port_; }

 private:
  bool authorized(const QHttpServerRequest& req) const;
  QString stateJson() const;
  QByteArray spectrumJson() const;
  void broadcastSse();  // SSE 频谱广播（spec_timer_ 每帧向所有客户端写）

  radio::app::RadioSession& session_;
  QHttpServer http_;
  Config cfg_;
  uint16_t bound_port_ = 0;

  // 合成谱源（headless 下总线侧自产；GUI 模式 MainWindow 另持一份用于显示）
  static constexpr size_t kFftSize = 2048;
  QTimer spec_timer_;
  radio::dsp::SignalSynth synth_{kFftSize, 48000.0};
  radio::dsp::SpectrumAnalyzer analyzer_{kFftSize, 48000.0};
  std::vector<float> samples_;

  // PWA 薄片：QSO 内存队列（POST /api/qso 入队，/api/qso.adif 导出）
  QJsonArray qso_log_;
  QString web_root_;

  // SSE 频谱推送（273"M5 全量再上 SSE"提前落地）：
  // 每客户端持 move 来的 Responder；spec_timer_ 广播；断开由 write 失败清理
  struct SseClient {
    // QHttpServerResponder 只有移动构造和 QHttpServerStream* 那个构造函数，
    // 没有默认构造，所以 SseClient 必须带着 responder 一起建
    // （原来先 new 一个 responder 再当 SseClient* 塞进 QSharedPointer，类型不过）
    explicit SseClient(QHttpServerResponder r) : responder(std::move(r)) {}
    QHttpServerResponder responder;  // move 持有（非 QObject，需显式清理标志）
    bool dead = false;
  };
  QList<QSharedPointer<SseClient>> sse_clients_;
};

}  // namespace radio::bus
