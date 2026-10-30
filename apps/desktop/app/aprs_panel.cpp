// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/aprs_panel.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "app/aprs_map.h"
#include "app/aprs_messages_dialog.h"
#include "app/aprs_rf.h"
#include "app/network_audio.h"
#include "aprs/persist.h"
#include "core/audio_link.h"

#ifdef RADIO_SUITE_HAVE_SERIALPORT
#include <QSerialPortInfo>
#endif

namespace radio::app {
namespace {

struct IsServer {
  const char* host;
  quint16 port;
};
constexpr IsServer kServers[] = {
    {"rotate.aprs2.net", 14580},
    {"china.aprs2.net", 14580},
};

QString qs(const std::string& s) { return QString::fromStdString(s); }

QString shortTime(int64_t ms) {
  return QDateTime::fromMSecsSinceEpoch(ms).toString("hh:mm:ss");
}

}  // namespace

AprsPanel::AprsPanel(QWidget* parent) : QWidget(parent) {
  buildUi();

  sock_ = new QTcpSocket(this);
  connect(sock_, &QTcpSocket::connected, this, &AprsPanel::onConnected);
  connect(sock_, &QTcpSocket::readyRead, this, &AprsPanel::onReadyRead);
  connect(sock_, &QTcpSocket::disconnected, this,
          &AprsPanel::onDisconnected);
  connect(sock_, &QTcpSocket::errorOccurred, this, [this](auto) {
    if (want_connected_) onDisconnected();
  });

  kiss_ = new KissLink(this);
  connect(kiss_, &KissLink::ax25Frame, this, [this](const QByteArray& frame) {
    aprs::Ax25Frame ax;
    std::string err;
    const auto bytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(frame.constData()),
        static_cast<size_t>(frame.size()));
    if (!aprs::parseAx25(bytes, &ax, &err)) {
      appendLog("RF AX.25 解析失败：" + qs(err));
      return;
    }
    ++rf_packets_;
    handleTnc2(aprs::ax25ToTnc2(ax), true);
  });
  connect(kiss_, &KissLink::statusChanged, this, [this](const QString& text) {
    appendLog(text);
    emit statusMessage(text);
  });

  afsk_timer_ = new QTimer(this);
  afsk_timer_->setInterval(20);
  connect(afsk_timer_, &QTimer::timeout, this, &AprsPanel::pumpAfskTx);

  db_ = std::make_unique<aprs::AprsDb>();
  QString err;
  if (db_->open(&err) && db_->load(&stations_, &messages_, &err)) {
    appendLog(QString("APRS 库已载入：%1 台站 / %2 条消息")
                  .arg(stations_.size())
                  .arg(messages_.inbox().size()));
  } else if (!err.isEmpty()) {
    appendLog("APRS 库不可用：" + err);
  }
  map_->setStations(&stations_);
  refreshTable();

  persist_timer_ = new QTimer(this);
  persist_timer_->setInterval(30000);
  connect(persist_timer_, &QTimer::timeout, this, &AprsPanel::saveNow);
  persist_timer_->start();

  QTimer* tick = new QTimer(this);
  tick->setInterval(1000);
  connect(tick, &QTimer::timeout, this, &AprsPanel::onTick);
  tick->start();
}

AprsPanel::~AprsPanel() {
  abortAfskTx();  // 关闭面板时若正在发 AFSK，先回 RX
  saveNow();
}

void AprsPanel::buildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(4, 2, 4, 2);
  root->setSpacing(3);

  auto* head = new QHBoxLayout();
  status_ = new QLabel("APRS 未连接", this);
  status_->setMinimumWidth(150);
  head->addWidget(status_);
  head->addWidget(new QLabel("过滤器", this));
  filter_edit_ = new QLineEdit(this);
  filter_edit_->setPlaceholderText("如 r/31.2/121.5/300 或 m/150（空=默认 m/100）");
  filter_edit_->setToolTip(
      "APRS-IS 过滤器，空格分隔；支持 r/m/b/p/o/t/g/u/a/e/f/d/q 十二类");
  filter_edit_->setClearButtonEnabled(true);
  head->addWidget(filter_edit_, 2);
  connect_btn_ = new QPushButton("连接 APRS-IS", this);
  connect(connect_btn_, &QPushButton::clicked, this,
          &AprsPanel::toggleConnect);
  head->addWidget(connect_btn_);
  rf_btn_ = new QPushButton("RF 设置", this);
  connect(rf_btn_, &QPushButton::clicked, this, &AprsPanel::openRfSettings);
  head->addWidget(rf_btn_);
  root->addLayout(head);

  auto* head2 = new QHBoxLayout();
  igate_rf_is_ = new QCheckBox("iGate RF→IS", this);
  igate_rf_is_->setToolTip("把 RF 收到的包追加 qAR 后注入 APRS-IS（需真实呼号）");
  connect(igate_rf_is_, &QCheckBox::toggled, this,
          [this](bool on) { igate_rf_to_is_ = on; });
  head2->addWidget(igate_rf_is_);
  igate_is_rf_ = new QCheckBox("iGate IS→RF", this);
  igate_is_rf_->setToolTip("把 APRS-IS 包重写路径后发到 RF（本地法规/功率自查）");
  connect(igate_is_rf_, &QCheckBox::toggled, this,
          [this](bool on) { igate_is_to_rf_ = on; });
  head2->addWidget(igate_is_rf_);
  head2->addSpacing(8);
  head2->addWidget(new QLabel("发送路径", this));
  send_path_ = new QComboBox(this);
  send_path_->addItems({"APRS-IS", "RF（KISS/AFSK）", "两者"});
  head2->addWidget(send_path_);
  head2->addStretch(1);
  beacon_btn_ = new QPushButton("发信标", this);
  connect(beacon_btn_, &QPushButton::clicked, this, &AprsPanel::sendBeacon);
  head2->addWidget(beacon_btn_);
  msg_btn_ = new QPushButton("消息", this);
  connect(msg_btn_, &QPushButton::clicked, this, &AprsPanel::openMessages);
  head2->addWidget(msg_btn_);
  root->addLayout(head2);

  auto* split = new QSplitter(Qt::Horizontal, this);
  map_ = new AprsMapWidget(split);
  connect(map_, &AprsMapWidget::stationClicked, this,
          &AprsPanel::selectStation);
  split->addWidget(map_);

  table_ = new QTableWidget(0, 8, split);
  table_->setHorizontalHeaderLabels(
      {"呼号/对象", "坐标", "距离km", "速度kt", "航向", "最后", "包", "备注"});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setColumnWidth(0, 104);
  table_->setColumnWidth(1, 150);
  table_->setColumnWidth(2, 60);
  table_->setColumnWidth(3, 56);
  table_->setColumnWidth(4, 48);
  table_->setColumnWidth(5, 60);
  table_->setColumnWidth(6, 40);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setSortingEnabled(true);
  connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
    const auto rows = table_->selectionModel()
                          ? table_->selectionModel()->selectedRows()
                          : QModelIndexList{};
    if (rows.isEmpty()) return;
    const auto* item = table_->item(rows.front().row(), 0);
    if (item != nullptr) selectStation(item->data(Qt::UserRole).toString());
  });
  connect(table_, &QTableWidget::cellDoubleClicked, this,
          [this](int row, int) {
            const auto* item = table_->item(row, 0);
            if (item == nullptr) return;
            const auto* st =
                stations_.find(item->data(Qt::UserRole).toString().toStdString());
            if (st != nullptr && st->hasPosition())
              map_->setCenter(st->pos.lat, st->pos.lon,
                              std::max(map_->zoom(), 11));
            selectStation(item->data(Qt::UserRole).toString());
          });
  split->addWidget(table_);
  split->setStretchFactor(0, 3);
  split->setStretchFactor(1, 2);
  root->addWidget(split, 1);

  detail_ = new QLabel("点击地图或列表中的台站查看详情", this);
  detail_->setWordWrap(true);
  detail_->setMinimumHeight(34);
  detail_->setStyleSheet("color:#9fc4d4;");
  root->addWidget(detail_);
}

void AprsPanel::setLoginCall(const QString& call) {
  if (!call.trimmed().isEmpty()) login_call_ = call.trimmed().toUpper();
  const bool can_tx = login_call_ != "N0CALL";
  beacon_btn_->setEnabled(can_tx);
  msg_btn_->setEnabled(true);  // 只看收件箱也允许
  if (messages_dialog_ != nullptr) {
    messages_dialog_->setMyCall(login_call_);
    messages_dialog_->setCanSend(can_tx);
  }
  if (!can_tx) {
    beacon_btn_->setToolTip(
        "需真实呼号（HamLog settings 的 my_callsign）才能上行");
  }
}

void AprsPanel::setMyPosition(bool has, double lat, double lon) {
  has_pos_ = has;
  my_lat_ = lat;
  my_lon_ = lon;
  map_->setMyPosition(has, lat, lon);
  if (has && filter_edit_->text().trimmed().isEmpty())
    filter_edit_->setPlaceholderText(
        QString("默认范围 r/%1/%2/300（可改成 m/150）")
            .arg(lat, 0, 'f', 3)
            .arg(lon, 0, 'f', 3));
  refreshTable();
}

void AprsPanel::setNetworkAudio(NetworkAudio* audio) {
  if (audio == nullptr) abortAfskTx();  // 音频链断开前先收尾
  audio_ = audio;
  afsk_rx_ = aprs::AfskDemodulator(
      aprs::AfskConfig{core::kNetAudioRate, 1200.0, 1200.0, 2200.0});
}

void AprsPanel::setTxController(std::function<bool()> begin,
                                std::function<void()> end) {
  tx_begin_ = std::move(begin);
  tx_end_ = std::move(end);
}

bool AprsPanel::isConnected() const {
  return sock_ != nullptr && sock_->state() == QTcpSocket::ConnectedState;
}

bool AprsPanel::rfConnected() const {
  if (rf_mode_ == RfMode::Afsk) return audio_ != nullptr && audio_->active();
  return kiss_ != nullptr && kiss_->isConnected();
}

void AprsPanel::toggleConnect() {
  if (isConnected()) {
    want_connected_ = false;
    sock_->disconnectFromHost();
    connect_btn_->setText("连接 APRS-IS");
    return;
  }
  want_connected_ = true;
  server_index_ = 0;
  backoff_s_ = 0;
  connectIs();
}

void AprsPanel::connectIs() {
  const auto& server = kServers[server_index_ % (sizeof(kServers) / sizeof(kServers[0]))];
  status_->setText(QString("连接 %1:%2 …").arg(server.host).arg(server.port));
  connect_btn_->setText("断开 APRS-IS");
  sock_->connectToHost(server.host, server.port);
}

void AprsPanel::onConnected() {
  backoff_s_ = 0;
  reconnect_pending_ = false;
  if (want_connected_) connect_btn_->setText("断开 APRS-IS");
  const bool can_tx = login_call_ != "N0CALL";
  const auto pass = can_tx ? aprs::passcode(login_call_.toStdString()) : -1;
  std::string line = "user " + login_call_.toStdString() + " pass " +
                     std::to_string(pass) + " vers radio-suite 0.1";
  // 过滤器逐项校验，避免把无效行塞给服务器（旧面板 r/33/ 就是反例）
  QString filter = filter_edit_->text().trimmed();
  if (filter.isEmpty()) {
    filter = has_pos_ ? QString("r/%1/%2/300")
                            .arg(my_lat_, 0, 'f', 3)
                            .arg(my_lon_, 0, 'f', 3)
                      : QString("m/100");
  }
  std::string filter_str;
  bool filter_ok = true;
  for (const QString& token : filter.split(' ', Qt::SkipEmptyParts)) {
    aprs::FilterLine fl;
    std::string err;
    if (!aprs::parseFilterLine(token.toStdString(), &fl, &err)) {
      filter_ok = false;
      appendLog(QString("过滤器无效：%1（%2）").arg(token, qs(err)));
      continue;
    }
    if (!filter_str.empty()) filter_str.push_back(' ');
    filter_str += token.toStdString();
  }
  if (!filter_str.empty()) line += " filter " + filter_str;
  sock_->write(line.c_str());
  sock_->write("\r\n");
  status_->setText(QString("APRS-IS 已连接（%1%2）")
                       .arg(login_call_)
                       .arg(can_tx ? "，可上行" : "，只收"));
  appendLog(QString("APRS-IS 已连接：%1%2")
                .arg(login_call_)
                .arg(filter_ok ? "" : "（过滤器部分无效，已跳过）"));
  emit statusMessage("APRS-IS 已连接");
}

void AprsPanel::onDisconnected() {
  if (reconnect_pending_) return;  // abort() 会再次触发 disconnected
  sock_->abort();
  connect_btn_->setText("连接 APRS-IS");
  if (!want_connected_) {
    status_->setText("APRS 未连接");
    return;
  }
  reconnect_pending_ = true;
  server_index_ = (server_index_ + 1) %
                  static_cast<int>(sizeof(kServers) / sizeof(kServers[0]));
  backoff_s_ = backoff_s_ == 0 ? 5 : std::min(backoff_s_ * 2, 60);
  status_->setText(QString("断开，%1 s 后重连 %2")
                       .arg(backoff_s_)
                       .arg(kServers[server_index_].host));
  QTimer::singleShot(backoff_s_ * 1000, this, [this] {
    reconnect_pending_ = false;
    if (want_connected_ && !isConnected()) connectIs();
  });
}

void AprsPanel::onReadyRead() {
  rx_buf_ += QString::fromUtf8(sock_->readAll());
  int nl = 0;
  while ((nl = rx_buf_.indexOf('\n')) >= 0) {
    const QString line = rx_buf_.left(nl).trimmed();
    rx_buf_.remove(0, nl + 1);
    if (line.isEmpty() || line.startsWith('#')) continue;
    handleTnc2(line.toStdString(), false);
  }
}

void AprsPanel::handleTnc2(const std::string& line, bool from_rf) {
  aprs::Packet packet;
  std::string err;
  if (!aprs::parseTnc2(line, &packet, &err)) {
    appendLog("丢弃：" + qs(err));
    return;
  }
  ingest(packet, from_rf);
}

void AprsPanel::ingest(aprs::Packet packet, bool from_rf) {
  const int64_t now = QDateTime::currentMSecsSinceEpoch();
  ++total_packets_;
  stations_.ingest(packet, now);
  if (messages_.ingest(packet, login_call_.toStdString(), now)) {
    appendLog(QString("新消息 ← %1：%2")
                  .arg(qs(packet.source), qs(packet.message.text)));
    if (messages_dialog_ != nullptr) {
      if (messages_dialog_->isVisible())
        messages_dialog_->reload(messages_);
    }
  }
  if (packet.kind == aprs::PacketKind::Message && !packet.message.ack &&
      !packet.message.rej && !packet.message.id.empty() &&
      aprs::callsignEqual(packet.message.addressee,
                          login_call_.toStdString())) {
    sendAckFor(packet, from_rf);
  }

  // iGate：RF→IS（qAR），IS→RF（重写路径 + 30 s 去重）
  const std::string key = aprs::dedupKey(packet);
  if (from_rf) {
    if (igate_rf_to_is_ && isConnected() && login_call_ != "N0CALL" &&
        aprs::isRfPacket(packet) && !is_dedup_.duplicate(key, now)) {
      sendIsRaw(aprs::toIsPacket(packet, login_call_.toStdString()));
    }
  } else if (igate_is_to_rf_ && rfConnected() &&
             aprs::mayGateToRf(packet,
                               aprs::IgatePolicy{login_call_.toStdString(),
                                                 true, true, false, 30,
                                                 "WIDE1-1,WIDE2-1"}) &&
             !is_dedup_.duplicate(key, now)) {
    sendViaRf(packet, "WIDE1-1,WIDE2-1");
  }
}

void AprsPanel::sendAckFor(const aprs::Packet& packet, bool from_rf) {
  if (login_call_ == "N0CALL" || packet.message.id.empty()) return;
  static QSet<QString> answered;  // 防 ACK 风暴（会话内）
  const QString key = qs(packet.source) + "|" + qs(packet.message.id);
  if (answered.contains(key)) return;
  answered.insert(key);
  if (answered.size() > 500) answered.clear();

  aprs::Packet ack;
  ack.source = login_call_.toStdString();
  ack.dest = "APRS";
  ack.raw_info = aprs::makeAck(packet.source, packet.message.id);
  if (from_rf) {
    if (rf_mode_ != RfMode::Off) sendViaRf(ack, "WIDE1-1,WIDE2-1");
    return;
  }
  ack.via = "TCPIP*";
  sendIsRaw(
      aprs::makeTnc2(ack.source, ack.dest, ack.via, ack.raw_info));
}

bool AprsPanel::transmit(const std::string& tnc2, const aprs::Packet& packet,
                         const std::string& rf_path) {
  bool sent = false;
  const int path = send_path_ ? send_path_->currentIndex() : 0;
  if (path != 1 && isConnected() && !tnc2.empty()) {
    sendIsRaw(tnc2);
    sent = true;
  }
  if (path != 0) sent = sendViaRf(packet, rf_path) || sent;
  return sent;
}

void AprsPanel::sendIsRaw(const std::string& line) {
  if (!isConnected()) {
    appendLog("APRS-IS 未连接，未上行");
    return;
  }
  sock_->write(line.c_str());
  sock_->write("\r\n");
}

bool AprsPanel::sendViaRf(const aprs::Packet& packet,
                          const std::string& rf_path) {
  if (rf_mode_ == RfMode::Off) return false;
  aprs::Packet p = packet;
  if (!rf_path.empty()) p.via = rf_path;
  const auto frame = aprs::tnc2ToAx25(p);
  if (!frame.has_value()) {
    appendLog("RF 封装失败：地址字段无效");
    return false;
  }
  const auto bytes = aprs::encodeAx25(*frame);

  if (rf_mode_ == RfMode::KissTcp || rf_mode_ == RfMode::KissSerial) {
    if (kiss_ == nullptr || !kiss_->isConnected()) {
      appendLog("KISS 未连接，RF 发送失败");
      return false;
    }
    return kiss_->sendAx25(bytes);
  }
  // AFSK：50003 音频链 + PTT
  if (audio_ == nullptr || !audio_->active()) {
    appendLog("50003 音频未开启，AFSK 无法发送");
    return false;
  }
  if (tx_active_) {
    appendLog("AFSK 正在发送，丢弃新包");
    return false;
  }
  if (tx_begin_ && !tx_begin_()) {
    appendLog("发射门禁拒绝当前频率，AFSK 未发送");
    return false;
  }
  tx_active_ = true;
  startAfskTx(
      aprs::afskModulate(bytes, aprs::AfskConfig{core::kNetAudioRate, 1200.0,
                                                 1200.0, 2200.0},
                         32, 2));
  return true;
}

void AprsPanel::startAfskTx(std::vector<float> audio) {
  afsk_tx_pcm_.resize(audio.size());
  for (size_t i = 0; i < audio.size(); ++i)
    afsk_tx_pcm_[i] = static_cast<int16_t>(
        std::lround(std::clamp(audio[i], -1.0f, 1.0f) * 32767.0f));
  afsk_tx_pos_ = 0;
  appendLog(QString("AFSK 发送 %1 ms").arg(afsk_tx_pcm_.size() * 1000 /
                                           core::kNetAudioRate));
  afsk_timer_->start();
  pumpAfskTx();
}

void AprsPanel::pumpAfskTx() {
  if (audio_ == nullptr) {
    abortAfskTx();  // 音频链被拆：不能把 PTT 留在键上
    return;
  }
  if (!tx_active_) {
    afsk_timer_->stop();
    return;
  }
  const size_t chunk = core::kNetAudioRate / 50;  // 20 ms
  if (afsk_tx_pos_ < afsk_tx_pcm_.size()) {
    const size_t n = std::min(chunk, afsk_tx_pcm_.size() - afsk_tx_pos_);
    audio_->queueTxPcm(
        std::span<const int16_t>(afsk_tx_pcm_.data() + afsk_tx_pos_, n));
    afsk_tx_pos_ += n;
    return;
  }
  afsk_timer_->stop();
  tx_active_ = false;
  if (tx_end_) tx_end_();
}

void AprsPanel::abortAfskTx() {
  if (afsk_timer_ != nullptr) afsk_timer_->stop();
  if (!tx_active_) return;
  tx_active_ = false;
  if (tx_end_) tx_end_();
}

void AprsPanel::feedRxPcm(std::span<const int16_t> pcm) {
  if (rf_mode_ != RfMode::Afsk) return;
  afsk_rx_.feed(pcm);
  std::vector<uint8_t> frame;
  while (afsk_rx_.pop(&frame)) {
    aprs::Ax25Frame ax;
    std::string err;
    if (!aprs::parseAx25(frame, &ax, &err)) {
      appendLog("AFSK AX.25 解析失败：" + qs(err));
      continue;
    }
    ++rf_packets_;
    handleTnc2(aprs::ax25ToTnc2(ax), true);
  }
}

void AprsPanel::sendBeacon() {
  if (login_call_ == "N0CALL") {
    emit statusMessage("需要真实呼号才能发信标");
    return;
  }
  if (!has_pos_) {
    emit statusMessage("未设置位置：请在 HamLog 设置 my_qth 或 my_grid");
    return;
  }
  const aprs::LatLon pos{my_lat_, my_lon_};
  const std::string info =
      aprs::makePosition(pos, aprs::Symbol{'/', '-'}, "radio-suite", false, true);
  aprs::Packet packet;
  packet.source = login_call_.toStdString();
  packet.dest = "APRS";
  packet.via = "TCPIP*";
  packet.raw_info = info;
  const bool is_path = send_path_ && send_path_->currentIndex() != 1;
  const std::string tnc2 = aprs::makeTnc2(
      packet.source, packet.dest, is_path ? "TCPIP*" : "WIDE1-1,WIDE2-1", info);
  if (!transmit(tnc2, packet, "WIDE1-1,WIDE2-1"))
    emit statusMessage("信标未发出：检查 APRS-IS/RF 连接与发送路径");
  else
    emit statusMessage(QString("信标已发送 %1 %2")
                           .arg(qs(aprs::formatLatitude(my_lat_)),
                                qs(aprs::formatLongitude(my_lon_))));
}

void AprsPanel::sendMessage() {
  openMessages();
}

void AprsPanel::openMessages() {
  if (messages_dialog_ == nullptr) {
    messages_dialog_ = new AprsMessagesDialog(this);
    messages_dialog_->setMyCall(login_call_);
    messages_dialog_->setCanSend(login_call_ != "N0CALL");
    connect(messages_dialog_, &AprsMessagesDialog::sendRequested, this,
            [this](const QString& to, const QString& text) {
              sendMessageTo(to.toStdString(), text.toStdString());
            });
    connect(messages_dialog_, &AprsMessagesDialog::markAllReadRequested, this,
            [this] {
              messages_.markAllRead();
              messages_dialog_->reload(messages_);
              refreshTable();
              saveNow();
            });
  }
  messages_.markAllRead();
  messages_dialog_->reload(messages_);
  messages_dialog_->show();
  messages_dialog_->raise();
  messages_dialog_->activateWindow();
}

void AprsPanel::sendMessageTo(const std::string& to, const std::string& text) {
  if (login_call_ == "N0CALL") {
    emit statusMessage("需要真实呼号才能发消息");
    return;
  }
  if (!isConnected() && !rfConnected()) {
    emit statusMessage("APRS-IS 与 RF 都未连接，消息未发送");
    return;
  }
  const int64_t now = QDateTime::currentMSecsSinceEpoch();
  const std::string id = messages_.enqueue(to, text, now);
  aprs::Packet packet;
  packet.source = login_call_.toStdString();
  packet.dest = "APRS";
  packet.via = "TCPIP*";
  packet.raw_info = aprs::makeMessage(to, text, id);
  const std::string tnc2 =
      aprs::makeTnc2(packet.source, packet.dest, "TCPIP*", packet.raw_info);
  if (transmit(tnc2, packet, "WIDE1-1,WIDE2-1")) {
    messages_.noteTry(id, now);
    emit statusMessage(QString("消息已发送 → %1（%2）").arg(qs(to), qs(id)));
  } else {
    emit statusMessage("消息发送失败：无可用通路");
  }
  if (messages_dialog_ != nullptr) messages_dialog_->reload(messages_);
  refreshTable();
  saveNow();
}

void AprsPanel::retryMessages() {
  const int64_t now = QDateTime::currentMSecsSinceEpoch();
  for (const auto* stale : messages_.due(now, 30000, 5)) {
    aprs::Packet packet;
    packet.source = login_call_.toStdString();
    packet.dest = "APRS";
    packet.via = "TCPIP*";
    packet.raw_info = aprs::makeMessage(stale->to, stale->text, stale->id);
    const std::string tnc2 = aprs::makeTnc2(packet.source, packet.dest,
                                            "TCPIP*", packet.raw_info);
    if (transmit(tnc2, packet, "WIDE1-1,WIDE2-1")) {
      messages_.noteTry(stale->id, now);
      appendLog(QString("重发消息 %1 → %2（第 %3 次）")
                    .arg(qs(stale->id), qs(stale->to))
                    .arg(stale->tries + 1));
    }
  }
}

void AprsPanel::onTick() {
  retryMessages();
  if (++tick_count_ % 2 == 0) refreshTable();  // 500 行表 2 s 刷一次足够
  if (messages_dialog_ != nullptr && messages_dialog_->isVisible())
    messages_dialog_->reload(messages_);
  const int unread = messages_.unread();
  if (unread != last_unread_) {
    last_unread_ = unread;
    msg_btn_->setText(unread > 0 ? QString("消息 (%1)").arg(unread) : "消息");
    emit unreadChanged(unread);
  }
}

void AprsPanel::refreshTable() {
  const QString keep = selected_;
  table_->setSortingEnabled(false);
  const int rows = static_cast<int>(stations_.size());
  if (table_->rowCount() != rows) table_->setRowCount(rows);
  int r = 0;
  for (const auto& [key, st] : stations_.all()) {
    auto* name = new QTableWidgetItem(qs(st.key));
    name->setData(Qt::UserRole, qs(st.key));
    if (st.is_object) name->setForeground(QColor(0xd0, 0x9c, 0xe8));
    else if (!st.alive) name->setForeground(QColor(150, 150, 150));
    else name->setForeground(QColor(0x6c, 0xd8, 0xe8));
    table_->setItem(r, 0, name);

    QString coord = "—";
    QString dist = "—";
    if (st.hasPosition())
      coord = QString("%1 %2")
                  .arg(qs(aprs::formatLatitude(st.pos.lat)),
                       qs(aprs::formatLongitude(st.pos.lon)));
    if (st.hasPosition() && has_pos_) {
      const double km = aprs::distanceKm(aprs::LatLon{my_lat_, my_lon_}, st.pos);
      dist = QString::number(km, 'f', km < 10 ? 1 : 0);
    }
    table_->setItem(r, 1, new QTableWidgetItem(coord));
    table_->setItem(r, 2, new QTableWidgetItem(dist));
    table_->setItem(r, 3, new QTableWidgetItem(
                               st.speed_kt >= 0
                                   ? QString::number(st.speed_kt, 'f', 0)
                                   : "—"));
    table_->setItem(r, 4, new QTableWidgetItem(
                               st.course_deg >= 0
                                   ? QString::number(st.course_deg, 'f', 0)
                                   : "—"));
    table_->setItem(r, 5, new QTableWidgetItem(shortTime(st.last_ms)));
    table_->setItem(r, 6,
                    new QTableWidgetItem(QString::number(st.packets)));
    QString note;
    if (st.weather.present) note = "天气 ";
    if (st.telemetry.present) note += "遥测 ";
    if (st.messaging) note += "消息 ";
    if (!st.comment.empty()) note += qs(st.comment).left(28);
    table_->setItem(r, 7, new QTableWidgetItem(note.trimmed()));
    ++r;
  }
  table_->setSortingEnabled(true);
  if (!keep.isEmpty()) {
    QSignalBlocker blocker(table_);
    for (int row = 0; row < table_->rowCount(); ++row) {
      const auto* item = table_->item(row, 0);
      if (item != nullptr && item->data(Qt::UserRole).toString() == keep) {
        table_->selectRow(row);
        break;
      }
    }
  }

  status_->setText(
      QString("APRS-IS %1 · 台站 %2 · 包 %3（RF %4）")
          .arg(isConnected() ? "已连接" : "未连接")
          .arg(stations_.size())
          .arg(total_packets_)
          .arg(rf_packets_));
  map_->refresh();
}

void AprsPanel::selectStation(const QString& key) {
  selected_ = key;
  map_->setSelected(key);
  const auto* st = stations_.find(key.toStdString());
  if (st == nullptr) return;
  QStringList parts;
  parts << (st->is_object ? QString("对象 %1（上报 %2）")
                                .arg(qs(st->name), qs(st->call))
                          : qs(st->key));
  if (st->hasPosition()) {
    parts << QString("%1 %2 · %3 %4")
                 .arg(qs(aprs::formatLatitude(st->pos.lat)),
                      qs(aprs::formatLongitude(st->pos.lon)),
                      qs(aprs::latLonToMaidenhead(st->pos)),
                      has_pos_ ? QString("%1 km").arg(
                                     aprs::distanceKm(
                                         aprs::LatLon{my_lat_, my_lon_}, st->pos),
                                     0, 'f', 1)
                               : QString());
    if (st->speed_kt >= 0)
      parts << QString("%1 kt / %2°").arg(st->speed_kt, 0, 'f', 0).arg(
          st->course_deg, 0, 'f', 0);
    if (st->altitude_ft > -1000)
      parts << QString("%1 m").arg(st->altitude_ft * 0.3048, 0, 'f', 0);
  }
  if (st->weather.present) {
    QString w = "天气";
    if (st->weather.has_temp) w += QString(" %1°C").arg(st->weather.temp_c, 0, 'f', 1);
    if (st->weather.has_wind)
      w += QString(" 风 %1°/%2kt").arg(st->weather.wind_dir_deg, 0, 'f', 0).arg(
          st->weather.wind_kt, 0, 'f', 0);
    parts << w;
  }
  if (!st->status.empty()) parts << "状态：" + qs(st->status);
  if (!st->comment.empty()) parts << "注释：" + qs(st->comment);
  parts << QString("经 %1 · %2 包 · 最后 %3")
               .arg(qs(st->via), QString::number(st->packets),
                    shortTime(st->last_ms));
  detail_->setText(parts.join(" · "));
}

void AprsPanel::openRfSettings() {
  QDialog dlg(this);
  dlg.setWindowTitle("APRS RF 设置");
  auto* form = new QFormLayout(&dlg);
  auto* mode = new QComboBox(&dlg);
  mode->addItems({"关闭", "KISS TCP", "KISS 串口", "AFSK（50003 音频）"});
  mode->setCurrentIndex(static_cast<int>(rf_mode_));
  form->addRow("RF 通路", mode);

  auto* host = new QLineEdit(kiss_host_, &dlg);
  auto* port = new QLineEdit(QString::number(kiss_port_num_), &dlg);
  form->addRow("KISS 主机", host);
  form->addRow("KISS 端口", port);
  auto* serial = new QLineEdit(serial_name_, &dlg);
  auto* baud = new QLineEdit(QString::number(serial_baud_), &dlg);
  form->addRow("串口", serial);
  form->addRow("波特率", baud);
#ifdef RADIO_SUITE_HAVE_SERIALPORT
  QStringList ports;
  for (const auto& info : QSerialPortInfo::availablePorts())
    ports << info.portName();
  serial->setPlaceholderText(ports.isEmpty() ? "无可用串口"
                                             : "可用：" + ports.join(", "));
#endif
  auto* note = new QLabel(
      "KISS 接 Dire Wolf / LoRa APRS 网关；AFSK 走电台 50003 音频，\n"
      "需先打开「网络音频」并处于可发射频率。", &dlg);
  note->setWordWrap(true);
  form->addRow(note);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                           &dlg);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  if (dlg.exec() != QDialog::Accepted) return;

  rf_mode_ = static_cast<RfMode>(mode->currentIndex());
  kiss_host_ = host->text().trimmed();
  kiss_port_num_ = static_cast<quint16>(port->text().toUShort());
  serial_name_ = serial->text().trimmed();
  serial_baud_ = baud->text().toInt();
  applyRfSettings();
}

void AprsPanel::applyRfSettings() {
  if (kiss_ != nullptr) kiss_->disconnectLink();
  abortAfskTx();  // 切 RF 通路/关音频时先回 RX，避免 PTT 悬空
  switch (rf_mode_) {
    case RfMode::Off:
      appendLog("RF 通路关闭");
      break;
    case RfMode::KissTcp:
      if (kiss_host_.isEmpty() || kiss_port_num_ == 0) {
        appendLog("KISS TCP 参数不完整");
        rf_mode_ = RfMode::Off;
        break;
      }
      kiss_->connectTcp(kiss_host_, kiss_port_num_);
      break;
    case RfMode::KissSerial:
#ifdef RADIO_SUITE_HAVE_SERIALPORT
      if (serial_name_.isEmpty()) {
        appendLog("未填写串口名");
        rf_mode_ = RfMode::Off;
        break;
      }
      kiss_->connectSerial(serial_name_, serial_baud_);
#else
      appendLog("本构建未启用串口（缺少 Qt6::SerialPort），请用 KISS TCP");
      rf_mode_ = RfMode::Off;
#endif
      break;
    case RfMode::Afsk:
      afsk_rx_.reset();
      appendLog(audio_ != nullptr && audio_->active()
                    ? "AFSK 已就绪：RX 解调 + TX 经 50003 音频"
                    : "AFSK 待命：请先打开「网络音频」");
      break;
  }
}

void AprsPanel::appendLog(const QString& text) {
  log_lines_ << QDateTime::currentDateTime().toString("hh:mm:ss ") + text;
  while (log_lines_.size() > 200) log_lines_.removeFirst();
  emit statusMessage(text);
}

void AprsPanel::saveNow() {
  if (!db_) return;
  QString err;
  if (!db_->save(stations_, messages_, &err) && !err.isEmpty())
    appendLog("APRS 保存失败：" + err);
}

}  // namespace radio::app
