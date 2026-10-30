// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/wsjtx_panel.h"

#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "app/wsjtx_client.h"

namespace radio::app {
namespace {

QString clockText(uint32_t time_ms) {
  const uint32_t ms = time_ms % 86'400'000u;
  return QString("%1:%2:%3")
      .arg(ms / 3'600'000u, 2, 10, QChar('0'))
      .arg((ms / 60'000u) % 60, 2, 10, QChar('0'))
      .arg((ms / 1000u) % 60, 2, 10, QChar('0'));
}

QString mhz(uint64_t hz) {
  return QString::number(static_cast<double>(hz) / 1e6, 'f', 6);
}

QString utcText(const radio::wsjtx::DateTime& dt) {
  return QDateTime::fromMSecsSinceEpoch(dt.utcMs()).toUTC().toString(
      "yyyy-MM-dd hh:mm:ss");
}

struct DecodeTarget {
  QString call;
  QString grid;
};

bool plausibleCall(const QString& token) {
  static const QSet<QString> kProtocol{"RR73", "RRR", "RR", "73", "R",
                                       "TU",   "DE",  "CQ", "QRZ"};
  if (kProtocol.contains(token.toUpper())) return false;
  if (token.size() < 3 || token.size() > 12) return false;
  bool letter = false, digit = false;
  for (const QChar c : token) {
    if (!c.isLetterOrNumber() && c != '/') return false;  // 排除 -12 / R-12 报告
    if (c.isLetter()) letter = true;
    if (c.isDigit()) digit = true;
  }
  return letter && digit;
}

bool looksLikeGrid(const QString& token) {
  static const QRegularExpression re(
      "^[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2})?$");
  return re.match(token).hasMatch();
}

// WSJT-X 文本：CQ [修饰…] CALL [GRID] / CALL1 CALL2 [GRID|报告|73]
// 修饰词（DX/TEST/POTA/SOTA/QRP/FD/WW/VHF…）都"不像呼号"，用同一判据筛掉，
// 不再维护一份必然漏项的清单。
DecodeTarget parseDecodeTarget(const QString& message) {
  DecodeTarget target;
  const QStringList tokens =
      message.trimmed().split(' ', Qt::SkipEmptyParts);
  if (tokens.isEmpty()) return target;
  for (int i = 1; i < tokens.size(); ++i) {
    const QString& token = tokens[i];
    if (looksLikeGrid(token)) {
      if (target.grid.isEmpty()) target.grid = token;
      continue;
    }
    if (target.call.isEmpty() && plausibleCall(token)) target.call = token;
  }
  return target;
}

QTableWidget* makeTable(const QStringList& headers, QWidget* parent) {
  auto* table = new QTableWidget(0, headers.size(), parent);
  table->setHorizontalHeaderLabels(headers);
  table->horizontalHeader()->setStretchLastSection(true);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->verticalHeader()->setVisible(false);
  table->setAlternatingRowColors(true);
  return table;
}

}  // namespace

WsjtxPanel::WsjtxPanel(QWidget* parent) : QWidget(parent) {
  buildUi();
}

WsjtxPanel::~WsjtxPanel() = default;

void WsjtxPanel::buildUi() {
  setWindowTitle("WSJT-X 接入 — radio-suite");
  resize(1000, 640);
  client_ = new WsjtxClient(this);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);

  auto* header = new QHBoxLayout();
  state_label_ = new QLabel("未监听", this);
  QFont f = state_label_->font();
  f.setBold(true);
  state_label_->setFont(f);
  client_label_ = new QLabel("客户端：—", this);
  radio_label_ = new QLabel("呼号/网格：— · 频率：—", this);
  tx_label_ = new QLabel("RX", this);
  header->addWidget(state_label_);
  header->addSpacing(12);
  header->addWidget(client_label_);
  header->addSpacing(12);
  header->addWidget(radio_label_, 1);
  header->addWidget(tx_label_);
  listen_btn_ = new QPushButton("开始监听", this);
  auto* settings_btn = new QPushButton("设置", this);
  header->addWidget(listen_btn_);
  header->addWidget(settings_btn);
  root->addLayout(header);

  tabs_ = new QTabWidget(this);
  decodes_ = makeTable({"时间", "SNR", "Δt", "Δf", "模式", "消息"}, this);
  decodes_->setColumnWidth(0, 80);
  decodes_->setColumnWidth(1, 55);
  decodes_->setColumnWidth(2, 60);
  decodes_->setColumnWidth(3, 70);
  decodes_->setColumnWidth(4, 70);
  decodes_->setToolTip("双击一行：把呼号/网格/频率送入主窗口（可再记 QSO）");
  tabs_->addTab(decodes_, "解码");

  qsos_ = makeTable({"时间(UTC)", "对方", "网格", "频率 MHz", "模式", "报告", "操作员"},
                    this);
  qsos_->setColumnWidth(0, 150);
  tabs_->addTab(qsos_, "QSO");

  wsprs_ = makeTable({"时间", "SNR", "频率 MHz", "呼号", "网格", "功率 dBm"}, this);
  wsprs_->setColumnWidth(0, 80);
  tabs_->addTab(wsprs_, "WSPR");

  adif_view_ = new QPlainTextEdit(this);
  adif_view_->setReadOnly(true);
  QFont mono("Consolas");
  mono.setStyleHint(QFont::Monospace);
  adif_view_->setFont(mono);
  tabs_->addTab(adif_view_, "ADIF");

  root->addWidget(tabs_, 1);

  auto* controls = new QHBoxLayout();
  reply_btn_ = new QPushButton("呼叫选中", this);
  reply_btn_->setToolTip("向 WSJT-X 发送 Reply（仅 CQ/QRZ 解码会被接受）");
  auto* halt_btn = new QPushButton("停止发射", this);
  halt_btn->setToolTip("向 WSJT-X 发送 Halt Tx");
  auto* replay_btn = new QPushButton("重放", this);
  replay_btn->setToolTip("请求 WSJT-X 重发当前 Band Activity 中的解码");
  auto* clear_btn = new QPushButton("清空本表", this);
  controls->addWidget(reply_btn_);
  controls->addWidget(halt_btn);
  controls->addWidget(replay_btn);
  controls->addWidget(clear_btn);
  controls->addSpacing(12);
  free_text_edit_ = new QLineEdit(this);
  free_text_edit_->setPlaceholderText("自由文本（留空并发送 = 发送当前 Tx5）");
  auto* send_text_btn = new QPushButton("发送文本", this);
  controls->addWidget(free_text_edit_, 1);
  controls->addWidget(send_text_btn);
  root->addLayout(controls);

  auto* location = new QHBoxLayout();
  location->addWidget(new QLabel("本机网格", this));
  grid_edit_ = new QLineEdit(this);
  grid_edit_->setPlaceholderText("PM01aa");
  grid_edit_->setMaximumWidth(120);
  location->addWidget(grid_edit_);
  auto* location_btn = new QPushButton("设置位置", this);
  location_btn->setToolTip("Location 覆盖 WSJT-X 会话内的网格（4/6 位）");
  location->addWidget(location_btn);
  auto* hint = new QLabel("UDP 2237 · WSJT-X 设置里需启用「Accept UDP requests」及相同端口", this);
  hint->setStyleSheet("color: #888;");
  location->addWidget(hint, 1);
  root->addLayout(location);

  footer_label_ = new QLabel("等待 WSJT-X 报文…", this);
  footer_label_->setWordWrap(true);
  root->addWidget(footer_label_);

  connect(listen_btn_, &QPushButton::clicked, this, [this] {
    if (client_->isListening())
      stopListening();
    else
      startListening(client_->group(), client_->port());
  });
  connect(decodes_, &QTableWidget::cellDoubleClicked, this,
          [this](int row, int) { activateDecode(row); });
  connect(settings_btn, &QPushButton::clicked, this, &WsjtxPanel::onSettings);
  connect(reply_btn_, &QPushButton::clicked, this, &WsjtxPanel::onReply);
  connect(halt_btn, &QPushButton::clicked, this,
          [this] { client_->haltTx(false); flash("已发送 Halt Tx"); });
  connect(replay_btn, &QPushButton::clicked, this, [this] {
    client_->replay();
    flash("已请求 WSJT-X 重放解码");
  });
  connect(clear_btn, &QPushButton::clicked, this, &WsjtxPanel::onClearDecodes);
  connect(send_text_btn, &QPushButton::clicked, this, &WsjtxPanel::onSendFreeText);
  connect(free_text_edit_, &QLineEdit::returnPressed, this,
          &WsjtxPanel::onSendFreeText);
  connect(location_btn, &QPushButton::clicked, this, &WsjtxPanel::onSetLocation);

  connect(client_, &WsjtxClient::statusReceived, this, &WsjtxPanel::applyStatus);
  connect(client_, &WsjtxClient::decodeReceived, this, &WsjtxPanel::addDecode);
  connect(client_, &WsjtxClient::qsoLogged, this,
          [this](const radio::wsjtx::QsoLogged& qso, const QString& id) {
            client_label_->setText(QString("客户端：%1").arg(id));
            addQso(qso);
          });
  connect(client_, &WsjtxClient::wsprDecode, this,
          [this](const radio::wsjtx::WsprDecode& decode, const QString& id) {
            client_label_->setText(QString("客户端：%1").arg(id));
            addWspr(decode);
          });
  connect(client_, &WsjtxClient::loggedAdif, this,
          [this](const QString& adif, const QString& id) {
            client_label_->setText(QString("客户端：%1").arg(id));
            setAdif(adif);
          });
  connect(client_, &WsjtxClient::heartbeatReceived, this,
          [this](const QString& id, const QString& version, const QString& revision) {
            last_client_ = id;
            client_label_->setText(
                QString("客户端：%1 · v%2%3")
                    .arg(id, version,
                         revision.isEmpty() ? QString()
                                            : QString(" (%1)").arg(revision)));
            tabs_->setTabText(0, QString("解码 · %1").arg(id));
          });
  connect(client_, &WsjtxClient::clientClosed, this, [this](const QString& id) {
    flash(QString("WSJT-X 客户端已关闭：%1").arg(id));
    tabs_->setTabText(0, "解码");
  });
  connect(client_, &WsjtxClient::error, this,
          [this](const QString& message) { flash(message, true); });
  connect(client_, &WsjtxClient::listeningChanged, this,
          [this](bool) { refreshListenButton(); });

  refreshListenButton();
}

bool WsjtxPanel::isListening() const {
  return client_ && client_->isListening();
}

void WsjtxPanel::setMyGrid(const QString& grid) {
  if (!grid.trimmed().isEmpty()) grid_edit_->setText(grid.trimmed());
}

void WsjtxPanel::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (auto_started_ || client_->isListening()) return;
  auto_started_ = true;
  QSettings st;
  const QString group =
      st.value("wsjtx/group", QString::fromLatin1(radio::wsjtx::kDefaultGroup))
          .toString();
  const uint port = st.value("wsjtx/port", radio::wsjtx::kDefaultPort).toUInt();
  startListening(group, static_cast<quint16>(port == 0 ? radio::wsjtx::kDefaultPort
                                                       : port));
}

void WsjtxPanel::startListening(const QString& group, quint16 port) {
  if (client_->isListening()) client_->stop();
  QString error;
  if (!client_->start(group, port, &error)) {
    flash(error, true);
    refreshListenButton();
    return;
  }
  auto_started_ = true;
  refreshListenButton();
  flash(QString("监听中：%1:%2").arg(group).arg(client_->port()));
}

void WsjtxPanel::stopListening() {
  if (!client_->isListening()) return;
  client_->stop();
  refreshListenButton();
  flash("已停止监听");
}

void WsjtxPanel::refreshListenButton() {
  const bool listening = client_->isListening();
  listen_btn_->setText(listening ? "停止监听" : "开始监听");
  if (listening) {
    state_label_->setText(QString("监听中 %1:%2").arg(client_->group()).arg(client_->port()));
    state_label_->setStyleSheet("color: #69f0ae;");
  } else {
    state_label_->setText("未监听");
    state_label_->setStyleSheet("color: #ff5252;");
  }
}

void WsjtxPanel::applyStatus(const radio::wsjtx::Status& status,
                             const QString& client_id) {
  last_client_ = client_id;
  last_dial_hz_ = status.dial_frequency;
  client_label_->setText(QString("客户端：%1").arg(client_id));
  radio_label_->setText(
      QString("%1 %2 · %3 MHz %4%5")
          .arg(QString::fromStdString(status.de_call),
               QString::fromStdString(status.de_grid), mhz(status.dial_frequency),
               QString::fromStdString(status.mode),
               status.dx_call.empty()
                   ? QString()
                   : QString(" · DX %1").arg(QString::fromStdString(status.dx_call))));
  if (status.transmitting) {
    tx_label_->setText("TX");
    tx_label_->setStyleSheet("color: #ff5252; font-weight: bold;");
  } else if (status.tx_enabled) {
    tx_label_->setText("TX 就绪");
    tx_label_->setStyleSheet("color: #f9a825; font-weight: bold;");
  } else {
    tx_label_->setText("RX");
    tx_label_->setStyleSheet("color: #69f0ae;");
  }
  if (!status.tx_message.empty())
    footer_label_->setText(
        QString("Tx：%1").arg(QString::fromStdString(status.tx_message)));
}

void WsjtxPanel::addDecode(const radio::wsjtx::Decode& decode,
                           const QString& client_id) {
  last_client_ = client_id;
  decodes_cache_.insert(decodes_cache_.begin(), decode);
  decodes_->insertRow(0);
  const QStringList cells{
      clockText(decode.time_ms),
      QString::number(decode.snr),
      QString::number(decode.delta_time, 'f', 1),
      QString::number(decode.delta_frequency),
      QString::fromStdString(decode.mode),
      QString::fromStdString(decode.message),
  };
  const bool call = cells[5].startsWith("CQ ") || cells[5].startsWith("QRZ");
  for (int col = 0; col < cells.size(); ++col) {
    auto* item = new QTableWidgetItem(cells[col]);
    if (decode.low_confidence) item->setForeground(QColor(0xb0, 0x8a, 0x4a));
    if (decode.off_air) item->setForeground(QColor(0x88, 0x88, 0x88));
    if (call) {
      QFont f = item->font();
      f.setBold(true);
      item->setFont(f);
      item->setBackground(QColor(0x2d, 0x4a, 0x2d));
    }
    decodes_->setItem(0, col, item);
  }
  if (decodes_cache_.size() > 500) {
    decodes_cache_.pop_back();
    decodes_->removeRow(decodes_->rowCount() - 1);
  }
  const QString tx = QString("Tx：%1").arg(QString::fromStdString(decode.message));
  if (decode.is_new) footer_label_->setText(tx);
}

void WsjtxPanel::addQso(const radio::wsjtx::QsoLogged& qso) {
  qsos_->insertRow(0);
  const QStringList cells{
      utcText(qso.date_time_off),
      QString::fromStdString(qso.dx_call),
      QString::fromStdString(qso.dx_grid),
      mhz(qso.tx_frequency),
      QString::fromStdString(qso.mode),
      QString("%1 / %2")
          .arg(QString::fromStdString(qso.report_sent),
               QString::fromStdString(qso.report_received)),
      QString::fromStdString(qso.operator_call),
  };
  for (int col = 0; col < cells.size(); ++col)
    qsos_->setItem(0, col, new QTableWidgetItem(cells[col]));
  if (qsos_->rowCount() > 200) qsos_->removeRow(qsos_->rowCount() - 1);
  tabs_->setTabText(1, "QSO · 最近 1 条");
  flash(QString("WSJT-X 已记录 QSO：%1 %2 MHz %3")
            .arg(QString::fromStdString(qso.dx_call), mhz(qso.tx_frequency),
                 QString::fromStdString(qso.mode)));
}

void WsjtxPanel::addWspr(const radio::wsjtx::WsprDecode& decode) {
  wsprs_->insertRow(0);
  const QStringList cells{
      clockText(decode.time_ms),
      QString::number(decode.snr),
      mhz(decode.frequency),
      QString::fromStdString(decode.callsign),
      QString::fromStdString(decode.grid),
      QString::number(decode.power_dbm),
  };
  for (int col = 0; col < cells.size(); ++col)
    wsprs_->setItem(0, col, new QTableWidgetItem(cells[col]));
  if (wsprs_->rowCount() > 500) wsprs_->removeRow(wsprs_->rowCount() - 1);
  tabs_->setTabText(2, "WSPR · 最近 1 条");
}

void WsjtxPanel::setAdif(const QString& adif) {
  adif_view_->setPlainText(adif);
  tabs_->setTabText(3, "ADIF · 最近 1 条");
}

bool WsjtxPanel::activateDecode(int row) {
  if (row < 0 || static_cast<size_t>(row) >= decodes_cache_.size()) {
    flash("请先在解码表选择一行", true);
    return false;
  }
  const auto& decode = decodes_cache_[static_cast<size_t>(row)];
  const QString message = QString::fromStdString(decode.message);
  const DecodeTarget target = parseDecodeTarget(message);
  if (target.call.isEmpty()) {
    flash(QString("无法从解码识别呼号：%1").arg(message), true);
    return false;
  }
  quint64 hz = last_dial_hz_;
  if (hz != 0) {
    // delta_frequency 在协议里是有符号 qint32：负偏移意味着低于刻度频率
    const int64_t target_hz =
        static_cast<int64_t>(hz) + decode.delta_frequency;
    if (target_hz > 0) hz = static_cast<quint64>(target_hz);
  }
  const QString mode = QString::fromStdString(decode.mode);
  emit decodeActivated(target.call, target.grid, hz, mode);
  flash(QString("已送入主窗口：%1%2")
            .arg(target.call,
                 hz == 0 ? QString()
                         : QString(" · %1 MHz").arg(hz / 1e6, 0, 'f', 6)));
  return true;
}

void WsjtxPanel::onReply() {
  const int row = decodes_->currentRow();
  if (row < 0 || static_cast<size_t>(row) >= decodes_cache_.size()) {
    flash("请先在解码表选择一条 CQ/QRZ", true);
    return;
  }
  const auto& decode = decodes_cache_[static_cast<size_t>(row)];
  if (!client_->sendReply(decode)) {
    flash("Reply 发送失败：未监听", true);
    return;
  }
  flash(QString("已发送 Reply → %1").arg(QString::fromStdString(decode.message)));
}

void WsjtxPanel::onSendFreeText() {
  const QString text = free_text_edit_->text();
  if (!client_->freeText(text, true)) {
    flash("自由文本发送失败：未监听", true);
    return;
  }
  free_text_edit_->clear();
  flash(text.isEmpty() ? "已请求发送当前 Tx5" : QString("已设置并发送自由文本"));
}

void WsjtxPanel::onSetLocation() {
  const QString grid = grid_edit_->text().trimmed();
  if (grid.isEmpty()) {
    flash("请输入 4/6 位 Maidenhead 网格", true);
    return;
  }
  if (!client_->setLocation(grid)) {
    flash("Location 发送失败：未监听", true);
    return;
  }
  QSettings st;
  st.setValue("wsjtx/my_grid", grid);
  flash(QString("已把操作位置设为 %1").arg(grid));
}

void WsjtxPanel::onClearDecodes() {
  decodes_cache_.clear();
  decodes_->setRowCount(0);
  flash("已清空本机解码表（未清 WSJT-X）");
}

void WsjtxPanel::onSettings() {
  QDialog dialog(this);
  dialog.setWindowTitle("WSJT-X UDP 设置");
  auto* form = new QFormLayout(&dialog);
  auto* group = new QLineEdit(client_->group(), &dialog);
  group->setToolTip("组播 224.0.0.1（默认）或单播地址");
  auto* port = new QSpinBox(&dialog);
  port->setRange(1, 65535);
  port->setValue(client_->port());
  auto* id = new QLineEdit(client_->clientId(), &dialog);
  form->addRow("组播/单播地址", group);
  form->addRow("UDP 端口", port);
  form->addRow("本端 ID", id);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                       &dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;

  const QString new_group = group->text().trimmed();
  const quint16 new_port = static_cast<quint16>(port->value());
  const QString new_id = id->text().trimmed();
  if (new_group.isEmpty()) {
    flash("地址不能为空", true);
    return;
  }
  QSettings st;
  st.setValue("wsjtx/group", new_group);
  st.setValue("wsjtx/port", new_port);
  client_->setClientId(new_id);
  const bool was_listening = client_->isListening();
  if (was_listening) client_->stop();
  startListening(new_group, new_port);
  if (!client_->isListening()) return;
  st.setValue("wsjtx/id", new_id);
}

void WsjtxPanel::flash(const QString& message, bool error) {
  footer_label_->setText(message);
  footer_label_->setStyleSheet(error ? "color: #ff5252;" : "color: #69f0ae;");
  emit statusMessage(message, error);
}

}  // namespace radio::app
