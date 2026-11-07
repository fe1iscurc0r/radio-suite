// SPDX-License-Identifier: AGPL-3.0-or-later
#include "bus/bus_server.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QUrlQuery>

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
    broadcastSse();
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

void BusServer::broadcastSse() {
  const QByteArray payload = spectrumJson();
  const QByteArray frame = "data: " + payload + "\n\n";
  for (auto it = sse_clients_.begin(); it != sse_clients_.end();) {
    auto &c = **it;
    if (c.dead) { it = sse_clients_.erase(it); continue; }
    // chunked 续写：responder 的 d-pointer 里 stream 是 QPointer，连接断了就自动变空，
    // 所以这里对已断开的客户端写是安全的空操作（不会野指针），只是没法据此清理
    c.responder.writeChunk(frame);
    ++it;
  }
}

bool BusServer::start(const Config& cfg, QString* error) {
  cfg_ = cfg;
  // 工单安全要求：非环回绑定必须带 token
  if (!QHostAddress(cfg_.bind_addr).isLoopback() && cfg_.token.isEmpty()) {
    if (error) *error = "非环回绑定必须提供 --token（工单 M5 安全要求）";
    return false;
  }

  // PWA 静态件（apps/web/）：exe 目录向上找仓库根；找不到回退旧演示页
  web_root_ = [] {
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
      const QString cand = d.absoluteFilePath("apps/web");
      if (QFile::exists(cand + "/index.html")) return cand;
      if (!d.cdUp()) break;
    }
    return QString();
  }();
  auto serveWebFile = [&](const QString& rel) {
    QFile f(web_root_ + "/" + rel);
    if (!f.open(QIODevice::ReadOnly))
      return QByteArray("PWA assets not found (apps/web/)");
    return f.readAll();
  };
  // PWA 静态件一律 no-store。总线就跑在开发机上，改完 app.js 同端口刷新还在跑旧脚本
  // ——没有缓存头时浏览器按启发式缓存，连 ignoreCache 的顶层导航都换不掉子资源，
  // 真机验证 SSE 时就是这么被旧 app.js 骗过一次；sw.js 被缓存住更等于整套旧页面续命。
  auto webFile = [serveWebFile](const QByteArray& mimeType, const char* rel) {
    QHttpServerResponse resp(mimeType, serveWebFile(QString::fromUtf8(rel)));
    auto hdr = resp.headers();
    hdr.append(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
    resp.setHeaders(std::move(hdr));
    return resp;
  };
  http_.route("/", [webFile] { return webFile("text/html", "index.html"); });
  http_.route("/app.css", [webFile] { return webFile("text/css", "app.css"); });
  http_.route("/app.js", [webFile] { return webFile("text/javascript", "app.js"); });
  http_.route("/sw.js", [webFile] { return webFile("text/javascript", "sw.js"); });
  http_.route("/manifest.webmanifest",
              [webFile] { return webFile("application/manifest+json", "manifest.webmanifest"); });
  http_.route("/icon.svg", [webFile] { return webFile("image/svg+xml", "icon.svg"); });
  // 旧演示页保留在 /demo（无 PWA 资产时的对照）
  http_.route("/demo", [] {
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
  // ---- PWA 控制出口（273 阶段二薄片：PTT/调谐，走 session 唯一出口）----
  http_.route("/api/control", QHttpServerRequest::Method::Post,
              [this](const QHttpServerRequest& req) {
    if (!authorized(req))
      return QHttpServerResponse(QHttpServerResponder::StatusCode::Unauthorized);
    const auto doc = QJsonDocument::fromJson(req.body());
    if (!doc.isObject())
      return QHttpServerResponse(QByteArrayLiteral("application/json"),
                                 QByteArrayLiteral("{\"error\":\"bad json\"}"));
    const QString action = doc.object().value("action").toString();
    QJsonObject result;
    if (action == "ptt_on") {
      session_.pttButtonPressed();  // 两击门禁由会话编排；PWA 第二击再调即确认
      result = {{"ptt", session_.gate().phase() ==
                                       radio::core::PttPhase::Tx
                                   ? "tx"
                                   : "confirm_pending"}};
    } else if (action == "ptt_off") {
      session_.pttButtonPressed();
      session_.pttButtonPressed();
      result = {{"ptt", "rx"}};
    } else if (action == "tune") {
      const auto hz = doc.object().value("freq").toVariant().toULongLong();
      result = {{"tuned", session_.tuneTo(hz)}};
      if (!session_.tuneTo(hz)) result = {{"error", "rejected"}};
    } else if (action == "set_mode") {
      const auto m = radio::core::civ::modeFromName(
          doc.object().value("mode").toString());
      if (!m) return QHttpServerResponse(QByteArrayLiteral("application/json"),
                                         QByteArrayLiteral("{\"error\":\"bad mode\"}"));
      session_.setMode(*m);
      result = {{"mode", radio::core::civ::modeName(*m)}};
    } else {
      return QHttpServerResponse(QByteArrayLiteral("application/json"),
                                 QByteArrayLiteral("{\"error\":\"unknown action\"}"));
    }
    return QHttpServerResponse(
        QByteArrayLiteral("application/json"),
        QJsonDocument(QJsonObject{{"ok", true}, {"result", result}})
            .toJson(QJsonDocument::Compact));
  });
  // ---- QSO 薄片录入：内存队列 → ADIF 文本（273 验收：grep <CALL: ≥1）----
  http_.route("/api/qso", QHttpServerRequest::Method::Post,
              [this](const QHttpServerRequest& req) {
    if (!authorized(req))
      return QHttpServerResponse(QHttpServerResponder::StatusCode::Unauthorized);
    const auto doc = QJsonDocument::fromJson(req.body());
    const auto obj = doc.object();
    QJsonObject q = obj.value("qso").toObject();
    if (q.isEmpty()) q = obj;
    if (q.value("callsign").toString().trimmed().isEmpty())
      return QHttpServerResponse(QByteArrayLiteral("application/json"),
                                 QByteArrayLiteral("{\"error\":\"callsign required\"}"));
    q["logged_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    qso_log_.append(q);
    return QHttpServerResponse(QByteArrayLiteral("application/json"),
                               QJsonDocument(QJsonObject{{"ok", true},
                                                         {"count", static_cast<double>(qso_log_.size())}})
                                   .toJson(QJsonDocument::Compact));
  });
  http_.route("/api/qso.adif", [this](const QHttpServerRequest& req) {
    if (!authorized(req))
      return QHttpServerResponse(QHttpServerResponder::StatusCode::Unauthorized);
    QString out = QStringLiteral(
        "ADIF 导出（PWA 薄片，273 阶段二验收）\n");
    for (const QJsonValue& v : qso_log_) {
      const QJsonObject q = v.toObject();
      const auto f = [&q](const char* k) { return q.value(k).toString(); };
      out += QString("<CALL:%1>%2 ").arg(f("callsign").length()).arg(f("callsign"));
      if (!f("freq_mhz").isEmpty())
        out += QString("<FREQ:%1>%2 ").arg(f("freq_mhz").length()).arg(f("freq_mhz"));
      if (!f("mode").isEmpty())
        out += QString("<MODE:%1>%2 ").arg(f("mode").length()).arg(f("mode"));
      if (!f("rst_sent").isEmpty())
        out += QString("<RST_SENT:%1>%2 ").arg(f("rst_sent").length()).arg(f("rst_sent"));
      out += "<EOR>\n";
    }
    return QHttpServerResponse(QByteArrayLiteral("text/plain"), out.toUtf8());
  });
  // ---- SSE 频谱实时推送（273"M5 全量再上 SSE"提前落地）----
  // 客户端 EventSource 收 data: {"power_db":[...],"frame_seq":N}


  // Qt 6.8.3 的 ViewTraits 只认 QHttpServerResponder&（写成 && 会撞
  // static_assert：can only be passed as a reference），要长期持有就自己 move 走
  //
  // 必须走 chunked：responder.write(data, mime) 发的是「一个完整响应」，头里带
  // content-length: 23。裸 socket 能继续读到后面的帧，但浏览器 EventSource 按
  // content-length 读完 23 字节就认为流结束、断开重连——于是每几秒攒一个死客户端，
  // 而频谱一帧都进不去。writeBeginChunked/writeChunk 才是 SSE 要的形状。
  http_.route("/api/spectrum/stream",
              [this](const QHttpServerRequest &, QHttpServerResponder &responder) {
    // SSE 契约：先下一行 event: hello，让 EventSource 立刻进入 open 态
    responder.writeBeginChunked(QByteArrayLiteral("text/event-stream"));
    responder.writeChunk(QByteArray("event: hello\ndata: {}\n\n"));
    // 上限是硬上限：超了就挤掉最老的那个（给它一个正常的流结束，别让它悬着）
    if (sse_clients_.size() >= kMaxSseClients) {
      const auto oldest = sse_clients_.takeFirst();
      if (!oldest->dead) oldest->responder.writeEndChunked(QByteArray());
    }
    sse_clients_.append(QSharedPointer<SseClient>::create(std::move(responder)));
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
