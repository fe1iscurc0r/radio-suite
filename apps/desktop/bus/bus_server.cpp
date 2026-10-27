// SPDX-License-Identifier: AGPL-3.0-or-later
#include "bus/bus_server.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>

namespace radio::bus {

// 极简订阅演示页：浏览器打开即验证总线活着（lumo/scratchpad 侧同理起订）
static const char* kDemoPage = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>radio-suite bus</title>
<style>body{background:#0b0e14;color:#cfe;font-family:monospace;margin:16px}
canvas{width:100%;height:180px;background:#070a10}
#st{font-size:18px;margin:6px 0}</style></head><body>
<div id="st">…</div><canvas id="cv" width="1024" height="180"></canvas>
<div>GET /api/state 每 250ms · GET /api/spectrum 每 66ms</div>
<script>
const cv=document.getElementById('cv'),g=cv.getContext('2d');
async function st(){const r=await fetch('/api/state');const j=await r.json();
 document.getElementById('st').textContent=
  (j.freq_hz/1e6).toFixed(6)+' MHz · '+j.mode+' · '+(j.ptt_on?'TX':'RX')+
  ' · S'+(j.smeter_raw*9/120).toFixed(1);}
async function sp(){const r=await fetch('/api/spectrum');const j=await r.json();
 const d=j.power_db,n=d.length;g.clearRect(0,0,cv.width,cv.height);
 g.strokeStyle='#6f6';g.beginPath();
 for(let i=0;i<n;i++){const x=i/n*cv.width,y=170-Math.max(0,Math.min(1,(d[i]+110)/100))*160;
  i?g.lineTo(x,y):g.moveTo(x,y);}g.stroke();}
setInterval(st,250);setInterval(sp,66);st();sp();
</script></body></html>)HTML";

BusServer::BusServer(radio::app::RadioSession& session, QObject* parent)
    : QObject(parent), session_(session) {
  samples_.assign(kFftSize, 0.0f);
  using Tone = radio::dsp::SignalSynth::Tone;
  synth_.setTones({Tone{7'500.0, 0.5f, 260.0}, Tone{15'000.0, 0.12f, 0.0},
                   Tone{21'000.0, 0.25f, 650.0}});
  connect(&spec_timer_, &QTimer::timeout, this, [this] {
    synth_.fill(samples_.data());
    analyzer_.process(samples_.data(), samples_.size());
  });
}

bool BusServer::authorized(const QHttpServerRequest& req) const {
  if (cfg_.token.isEmpty()) return true;
  if (QUrlQuery(req.url()).queryItemValue("token") == cfg_.token) return true;
  // Qt 6.7+ headers() 返回 QHttpHeaders（非列表），用 value() 取
  const QString hdr = QString::fromUtf8(
      req.headers().value(QByteArrayLiteral("X-Radio-Token")));
  return hdr == cfg_.token;
}

QString BusServer::stateJson() const {
  const auto s = session_.store().snapshot();
  QJsonObject j;
  j["freq_hz"] = static_cast<double>(s.freq_hz);
  j["mode"] = radio::core::civ::modeName(s.mode);
  j["filter"] = s.filter;
  j["ptt_on"] = s.ptt_on;
  j["smeter_raw"] = s.smeter_raw;
  j["smeter_dbm"] = static_cast<double>(s.smeter_dbm);
  j["att_20db"] = s.att_20db;
  j["nb"] = s.nb;
  j["nr"] = s.nr;
  j["last_update_ms"] = static_cast<double>(s.last_update_ms);
  return QString::fromUtf8(QJsonDocument(j).toJson(QJsonDocument::Compact));
}

QByteArray BusServer::spectrumJson() const {
  const auto& db = analyzer_.powerDb();
  QJsonObject j;
  j["frame_seq"] = static_cast<double>(analyzer_.frameSeq());
  j["bin_hz"] = analyzer_.binHz();
  QJsonArray arr;
  for (float v : db) arr.append(static_cast<double>(v));
  j["power_db"] = arr;
  return QJsonDocument(j).toJson(QJsonDocument::Compact);
}

bool BusServer::start(const Config& cfg, QString* error) {
  cfg_ = cfg;
  // 工单安全要求：非环回绑定必须带 token
  if (!QHostAddress(cfg_.bind_addr).isLoopback() && cfg_.token.isEmpty()) {
    if (error) *error = "非环回绑定必须提供 --token（工单 M5 安全要求）";
    return false;
  }

  http_.route("/", [] {
    return QHttpServerResponse(QByteArrayLiteral("text/html"),
                               QByteArray(kDemoPage));
  });
  http_.route("/api/state", [this](const QHttpServerRequest& req) {
    if (!authorized(req))
      return QHttpServerResponse(QHttpServerResponder::StatusCode::Unauthorized);
    return QHttpServerResponse(QByteArrayLiteral("application/json"),
                               stateJson().toUtf8());
  });
  http_.route("/api/spectrum", [this](const QHttpServerRequest& req) {
    if (!authorized(req))
      return QHttpServerResponse(QHttpServerResponder::StatusCode::Unauthorized);
    return QHttpServerResponse(QByteArrayLiteral("application/json"),
                               spectrumJson());
  });

  // Qt 6.8：自建 QTcpServer 监听后交给 QAbstractHttpServer::bind
  auto* srv = new QTcpServer(this);
  if (!srv->listen(QHostAddress(cfg_.bind_addr), cfg_.port) ||
      !http_.bind(srv)) {
    if (error) *error = QString("总线监听失败 %1:%2").arg(cfg_.bind_addr).arg(cfg_.port);
    return false;
  }
  bound_port_ = srv->serverPort();
  spec_timer_.start(66);  // ~15fps 快照源
  qInfo() << "bus: http://" + cfg_.bind_addr + ":" + QString::number(bound_port_) +
                 "/  (/api/state /api/spectrum)"
          << (cfg_.token.isEmpty() ? "" : "（token 已启用）");
  return true;
}

}  // namespace radio::bus
