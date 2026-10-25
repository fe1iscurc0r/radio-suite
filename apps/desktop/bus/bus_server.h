// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · bus/bus_server.h — 本机 HTTP 总线（scratchpad/lumo 集成契约，M5 第一片）
//
// 工单 M5：GET /api/state、GET /api/spectrum、POST /api/control；
// 默认 bind 127.0.0.1，非环回必须 --token（第一片即实现该安全要求）。
// 频谱流第一片用快照轮询（M5 全量再上 SSE/chunked）；总线自持合成谱源
// （GUI 各自一份，M2 真音频时统一为 dsp 线程单源）。
#pragma once

#include <QHttpServer>
#include <QObject>
#include <QTimer>
#include <vector>

#include "app/session.h"
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

 private:
  bool authorized(const QHttpServerRequest& req) const;
  QString stateJson() const;
  QByteArray spectrumJson() const;

  radio::app::RadioSession& session_;
  QHttpServer http_;
  Config cfg_;

  // 合成谱源（headless 下总线侧自产；GUI 模式 MainWindow 另持一份用于显示）
  static constexpr size_t kFftSize = 2048;
  QTimer spec_timer_;
  radio::dsp::SignalSynth synth_{kFftSize, 48000.0};
  radio::dsp::SpectrumAnalyzer analyzer_{kFftSize, 48000.0};
  std::vector<float> samples_;
};

}  // namespace radio::bus
