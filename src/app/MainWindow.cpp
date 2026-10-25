// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/MainWindow.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

#include "app/aprs_panel.h"

namespace {

struct BandButton {
  const char* name;
  uint64_t center_hz;
};

// 常用频段快捷（中心频率取 CW/SSB 常用段）
constexpr BandButton kBands[] = {
    {"40m", 7'100'000},  {"20m", 14'100'000}, {"15m", 21'200'000},
    {"10m", 28'500'000}, {"6m", 50'150'000},  {"2m", 145'500'000},
    {"70cm", 433'000'000},
};

// 步进档（Hz）：互斥按钮组，默认 1 kHz
constexpr qint64 kSteps[] = {100, 1'000, 10'000, 100'000};
const char* kStepNames[] = {"100Hz", "1k", "10k", "100k"};
constexpr int kDefaultStep = 1;

QString formatFreq(uint64_t hz) {
  return QString("%1.%2 MHz")
      .arg(hz / 1'000'000)
      .arg(hz % 1'000'000, 6, 10, QChar('0'));
}

}  // namespace

MainWindow::MainWindow(std::shared_ptr<radio::core::ICivTransport> transport,
                       QWidget* parent)
    : QMainWindow(parent), session_(std::move(transport)) {
  buildUi();

  samples_.assign(kFftSize, 0.0f);
  col_db_.assign(static_cast<size_t>(WaterfallWidget::kTexW), -120.0f);
  row_bytes_.assign(static_cast<size_t>(WaterfallWidget::kTexW), 0);

  // 演示谱：7.5kHz 强音（慢漂移）+ 15kHz 弱音 + 21kHz 中音（快漂移）+ 噪底
  using Tone = radio::dsp::SignalSynth::Tone;
  synth_.setTones({Tone{7'500.0, 0.5f, 260.0},
                   Tone{15'000.0, 0.12f, 0.0},
                   Tone{21'000.0, 0.25f, 650.0}});
  wf_proc_.db_floor = -110.0f;
  wf_proc_.db_ceil = -10.0f;

  connect(&session_, &radio::app::RadioSession::stateChanged, this,
          &MainWindow::refreshState);
  connect(&session_, &radio::app::RadioSession::pttPhaseChanged, this,
          &MainWindow::refreshPtt);
  connect(&session_, &radio::app::RadioSession::tuneRejected, this,
          &MainWindow::onTuneRejected);
  connect(&session_, &radio::app::RadioSession::txBlocked, this,
          &MainWindow::onTxBlocked);
  connect(&bench_timer_, &QTimer::timeout, this, &MainWindow::onBenchTick);
  bench_timer_.start(66);  // ~15 fps（M0 红线验证在 M2 做满帧压测）

  // 显示层刷新：倒计时/确认窗口剩余秒走秒（判定与死限都在 core）
  connect(&ui_timer_, &QTimer::timeout, this, &MainWindow::refreshPtt);
  ui_timer_.start(250);

  connect(&status_timer_, &QTimer::timeout, this, [this] {
    status_label_->setStyleSheet("");
    status_label_->setText("transport: mock · radio: IC-705 sim");
  });
  status_timer_.setSingleShot(true);

  session_.start();
  refreshState();
  refreshPtt();
}

void MainWindow::buildUi() {
  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);

  // ---- 顶栏：频率 / 模式 / S 表 / PTT ----
  auto* top = new QHBoxLayout();
  freq_label_ = new QLabel("__.__._ MHz", central);
  QFont mono("Cascadia Mono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPointSize(30);
  freq_label_->setFont(mono);
  top->addWidget(freq_label_, 1);

  mode_label_ = new QLabel("--", central);
  mode_label_->setAlignment(Qt::AlignCenter);
  mode_label_->setMinimumWidth(90);
  top->addWidget(mode_label_);

  smeter_label_ = new QLabel("S0", central);
  smeter_label_->setAlignment(Qt::AlignCenter);
  smeter_label_->setMinimumWidth(150);
  top->addWidget(smeter_label_);

  ptt_btn_ = new QPushButton("PTT", central);
  ptt_btn_->setMinimumHeight(56);
  ptt_btn_->setMinimumWidth(180);
  QFont bold = ptt_btn_->font();
  bold.setBold(true);
  bold.setPointSize(12);
  ptt_btn_->setFont(bold);
  connect(ptt_btn_, &QPushButton::clicked, this,
          [this] { session_.pttButtonPressed(); });
  top->addWidget(ptt_btn_);
  root->addLayout(top);

  // ---- 调谐行：频段快捷 / 步进档（互斥按钮组）/ 升降 ----
  auto* tune = new QHBoxLayout();
  for (const auto& b : kBands) {
    auto* btn = new QPushButton(b.name, central);
    btn->setProperty("center_hz", static_cast<qulonglong>(b.center_hz));
    connect(btn, &QPushButton::clicked, this, &MainWindow::onBandClicked);
    tune->addWidget(btn);
  }
  tune->addStretch(1);

  tune->addWidget(new QLabel("步进", central));
  for (int i = 0; i < 4; ++i) {
    auto* btn = new QPushButton(kStepNames[i], central);
    btn->setCheckable(true);
    btn->setChecked(i == kDefaultStep);
    btn->setProperty("step_hz", static_cast<qlonglong>(kSteps[i]));
    // 互斥：点了谁谁亮
    const int idx = i;
    connect(btn, &QPushButton::clicked, this, [this, idx] {
      for (int j = 0; j < static_cast<int>(step_btns_.size()); ++j)
        step_btns_[static_cast<size_t>(j)]->setChecked(j == idx);
    });
    step_btns_.push_back(btn);
    tune->addWidget(btn);
  }

  auto* down = new QPushButton("▼", central);
  auto* up = new QPushButton("▲", central);
  down->setMinimumWidth(48);
  up->setMinimumWidth(48);
  connect(down, &QPushButton::clicked, this, [this] { onTuneStep(-1); });
  connect(up, &QPushButton::clicked, this, [this] { onTuneStep(+1); });
  tune->addWidget(down);
  tune->addWidget(up);

  // 手动输频（MHz）+ 接收波段（区间中点，非点频；发射仍锁业余段）
  freq_edit_ = new QLineEdit(central);
  freq_edit_->setPlaceholderText("MHz，如 127.500");
  freq_edit_->setFixedWidth(120);
  freq_edit_->setAlignment(Qt::AlignRight);
  connect(freq_edit_, &QLineEdit::returnPressed, this, &MainWindow::onManualFreq);
  tune->addWidget(freq_edit_);
  auto* go = new QPushButton("GO", central);
  go->setMinimumWidth(44);
  connect(go, &QPushButton::clicked, this, &MainWindow::onManualFreq);
  tune->addWidget(go);
  // 接收波段：进区间中点 + 合适模式；▲▼ 步进在波段内连续可调
  auto* air = new QPushButton("AIR 118-137", central);
  air->setToolTip("民航话音波段（AM），进入波段中点 127.500，可听不可发");
  connect(air, &QPushButton::clicked, this, [this] {
    session_.setMode(radio::core::civ::Mode::AM);
    session_.tuneTo(127'500'000);
  });
  tune->addWidget(air);
  auto* bc = new QPushButton("BC-FM 87.5-108", central);
  bc->setToolTip("调频广播波段（WFM），进入波段中点 97.750，可听不可发");
  connect(bc, &QPushButton::clicked, this, [this] {
    session_.setMode(radio::core::civ::Mode::WFM);
    session_.tuneTo(97'750'000);
  });
  tune->addWidget(bc);
  auto* bcsw = new QPushButton("BC-SW", central);
  bcsw->setToolTip("短波广播（AM），进入 31m 段 9.500，可听不可发");
  connect(bcsw, &QPushButton::clicked, this, [this] {
    session_.setMode(radio::core::civ::Mode::AM);
    session_.tuneTo(9'500'000);
  });
  tune->addWidget(bcsw);
  root->addLayout(tune);

  // ---- 模式 + 功能行：模式组（回读驱动） / ATT / NB / NR / MONI ----
  auto* funcs = new QHBoxLayout();
  funcs->addWidget(new QLabel("模式", central));
  const std::pair<const char*, radio::core::civ::Mode> kModes[] = {
      {"LSB", radio::core::civ::Mode::LSB}, {"USB", radio::core::civ::Mode::USB},
      {"AM", radio::core::civ::Mode::AM},   {"CW", radio::core::civ::Mode::CW},
      {"FM", radio::core::civ::Mode::FM},   {"WFM", radio::core::civ::Mode::WFM}};
  for (const auto& [name, m] : kModes) {
    auto* btn = new QPushButton(name, central);
    btn->setCheckable(true);
    const auto mode = m;
    connect(btn, &QPushButton::clicked, this, [this, btn, mode] {
      session_.setMode(mode);
      btn->setChecked(true);  // 乐观更新，250ms 回读纠偏
    });
    mode_btns_.push_back(btn);
    funcs->addWidget(btn);
  }
  funcs->addStretch(1);

  auto mk_toggle = [&](const QString& name, auto setter) {
    auto* btn = new QPushButton(name, central);
    btn->setCheckable(true);
    connect(btn, &QPushButton::clicked, this, [btn, setter] { setter(btn->isChecked()); });
    funcs->addWidget(btn);
    return btn;
  };
  att_btn_ = mk_toggle("ATT 20dB", [this](bool on) { session_.setAtt(on); });
  nb_btn_ = mk_toggle("NB", [this](bool on) {
    session_.setSw16(radio::core::civ::kSub16Nb, on ? 1 : 0);
  });
  nr_btn_ = mk_toggle("NR", [this](bool on) {
    session_.setSw16(radio::core::civ::kSub16Nr, on ? 1 : 0);
  });
  moni_btn_ = mk_toggle("MONI", [this](bool on) {
    session_.setSw16(radio::core::civ::kSub16Moni, on ? 1 : 0);
  });
  notch_btn_ = mk_toggle("NOTCH", [this](bool on) {
    session_.setSw16(radio::core::civ::kSub16NotchAuto, on ? 1 : 0);
  });
  vox_btn_ = mk_toggle("VOX", [this](bool on) {
    session_.setSw16(radio::core::civ::kSub16Vox, on ? 1 : 0);
  });
  // 循环档按钮：点击按当前回读值切下一档（回读纠偏）
  pamp_btn_ = new QPushButton("PAMP", central);
  connect(pamp_btn_, &QPushButton::clicked, this, [this] {
    const int cur = session_.store().snapshot().pamp;
    session_.setSw16(radio::core::civ::kSub16Pamp,
                     static_cast<uint8_t>((cur + 1) % 3));
  });
  funcs->addWidget(pamp_btn_);
  agc_btn_ = new QPushButton("AGC", central);
  connect(agc_btn_, &QPushButton::clicked, this, [this] {
    const int cur = session_.store().snapshot().agc;
    session_.setSw16(radio::core::civ::kSub16Agc,
                     static_cast<uint8_t>(cur >= 3 ? 1 : cur + 1));
  });
  funcs->addWidget(agc_btn_);
  dup_btn_ = new QPushButton("DUP", central);
  connect(dup_btn_, &QPushButton::clicked, this, [this] {
    // 读回值 0x00=simplex(归一)/0x11/0x12；写入用 0x10/0x11/0x12
    const int cur = session_.store().snapshot().duplex_raw;
    const uint8_t next = cur == 0x00 || cur == 0x10 ? 0x11 : (cur == 0x11 ? 0x12 : 0x10);
    session_.setDuplex(next);
  });
  funcs->addWidget(dup_btn_);
  root->addLayout(funcs);

  // ---- 瀑布 ----
  waterfall_ = new WaterfallWidget(central);
  waterfall_->setDbRange(-110.0f, -10.0f);
  root->addWidget(waterfall_, 1);

  // ---- APRS-IS 台流（APRSLocus 融合第一片，可折叠）----
  aprs_panel_ = new radio::app::AprsPanel(central);
  aprs_panel_->hide();
  connect(aprs_panel_, &radio::app::AprsPanel::statusMessage, this,
          [this](const QString& m) { flashStatus(m, false); });
  root->addWidget(aprs_panel_);

  // ---- hamlog 融合行：记 QSO（写 HamLog 库）+ ADIF 导出 + APRS 开关 ----
  auto* logrow = new QHBoxLayout();
#ifdef RADIO_SUITE_HAVE_LOGBOOK
  const bool hamlog_ok = hamlog_.available();
  logrow->addWidget(new QLabel(
      hamlog_ok ? QString("HamLog：已连接（%1 条可见）").arg(hamlog_.recent(1).size())
                : "HamLog：库未找到（先运行一次 HamLog）",
      central));
  qso_call_edit_ = new QLineEdit(central);
  qso_call_edit_->setPlaceholderText("对方呼号，如 BG5ABC");
  qso_call_edit_->setFixedWidth(140);
  logrow->addWidget(qso_call_edit_);
  auto* log_btn = new QPushButton("记 QSO", central);
  log_btn->setEnabled(hamlog_ok);
  connect(log_btn, &QPushButton::clicked, this, &MainWindow::onLogQso);
  logrow->addWidget(log_btn);
  auto* adif_btn = new QPushButton("ADIF 导出", central);
  adif_btn->setEnabled(hamlog_ok);
  connect(adif_btn, &QPushButton::clicked, this, &MainWindow::onExportAdif);
  logrow->addWidget(adif_btn);
  aprs_panel_->setLoginCall(hamlog_ok ? hamlog_.myCallsign() : QString());
#else
  logrow->addWidget(new QLabel("日志模块未编译（缺 Qt6::Sql）", central));
#endif
  logrow->addStretch(1);
  auto* aprs_btn = new QPushButton("APRS 台流", central);
  aprs_btn->setCheckable(true);
  connect(aprs_btn, &QPushButton::toggled, this, [this](bool on) {
    aprs_panel_->setVisible(on);
    if (on && !aprs_panel_->isConnected()) aprs_panel_->toggleConnect();
  });
  logrow->addWidget(aprs_btn);
  root->addLayout(logrow);

#ifdef RADIO_SUITE_HAVE_LOGBOOK
  if (hamlog_ok) {
    qso_list_ = new QListWidget(central);
    qso_list_->setMaximumHeight(96);
    root->addWidget(qso_list_);
    refreshQsoList();
  }
#endif

  status_label_ = new QLabel("transport: mock · radio: IC-705 sim", central);
  root->addWidget(status_label_);

  setCentralWidget(central);
  setWindowTitle("radio-suite — M0 spike");
}

void MainWindow::syncTogglesFromState(const radio::core::RadioState& s) {
  // 回读驱动：模式/功能按钮以电台回读为准（点击时的乐观更新在此纠偏，
  // 含 NAK 场景——如 ATT 在非 HF 段被电台拒绝，按钮会弹回）
  const auto block = [](QPushButton* b, bool checked) {
    QSignalBlocker guard(b);
    b->setChecked(checked);
  };
  for (size_t i = 0; i < mode_btns_.size(); ++i)
    block(mode_btns_[i], static_cast<int>(s.mode) == static_cast<int>(i));
  block(att_btn_, s.att_20db);
  block(nb_btn_, s.nb);
  block(nr_btn_, s.nr);
  block(moni_btn_, s.moni);
  block(notch_btn_, s.notch_auto);
  block(vox_btn_, s.vox);
  // 循环档按钮：文字随回读值更新
  const char* pamp_txt[] = {"PAMP", "PAMP P1", "PAMP P2"};
  pamp_btn_->setText(pamp_txt[s.pamp <= 2 ? s.pamp : 0]);
  const char* agc_txt[] = {"AGC", "AGC F", "AGC M", "AGC S"};
  agc_btn_->setText(agc_txt[s.agc >= 1 && s.agc <= 3 ? s.agc : 2]);
  dup_btn_->setText(s.duplex_raw == 0x11 ? "DUP-" : (s.duplex_raw == 0x12 ? "DUP+" : "SPLX"));
}

void MainWindow::refreshState() {
  const auto s = session_.store().snapshot();
  freq_label_->setText(formatFreq(s.freq_hz));
  mode_label_->setText(QString("%1/%2").arg(radio::core::civ::modeName(s.mode)).arg(s.filter));
  smeter_label_->setText(
      QString("S%1 (%2 dBm)")
          .arg(s.smeter_raw * 9.0 / 120.0, 0, 'f', 1)
          .arg(s.smeter_dbm, 0, 'f', 1));
  syncTogglesFromState(s);
}

void MainWindow::refreshPtt() {
  using radio::core::PttPhase;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  switch (session_.gate().phase()) {
    case PttPhase::Rx:
      ptt_btn_->setText("PTT（RX）");
      ptt_btn_->setStyleSheet("QPushButton { background: #2e7d32; color: white; }");
      break;
    case PttPhase::ConfirmPending: {
      // 剩余 = 死限 - 当前（死限是绝对毫秒，别再直接除 1000 当秒数）
      qint64 left_s = (static_cast<qint64>(session_.gate().confirmDeadlineMs()) - now) / 1000;
      if (left_s < 0) left_s = 0;
      ptt_btn_->setText(QString("确认发射？（%1s 内再按）").arg(left_s));
      ptt_btn_->setStyleSheet("QPushButton { background: #f9a825; color: black; }");
      break;
    }
    case PttPhase::Tx: {
      qint64 left_s =
          (static_cast<qint64>(session_.gate().txDeadlineMs()) - now) / 1000;
      if (left_s < 0) left_s = 0;
      ptt_btn_->setText(
          QString("TX %1:%2（点击回 RX）")
              .arg(left_s / 60, 2, 10, QChar('0'))
              .arg(left_s % 60, 2, 10, QChar('0')));
      ptt_btn_->setStyleSheet("QPushButton { background: #c62828; color: white; }");
      break;
    }
  }
}

void MainWindow::onTuneRejected(quint64 hz) {
  flashStatus(QString("已拒绝 %1 MHz：超出接收范围（100kHz–1.45GHz）")
                  .arg(hz / 1'000'000.0, 0, 'f', 3),
              true);
}

void MainWindow::onTxBlocked(quint64 hz) {
  flashStatus(QString("发射被拒：%1 MHz 不在业余频段（收听开放，发射锁定白名单）")
                  .arg(hz / 1'000'000.0, 0, 'f', 3),
              true);
}

void MainWindow::onManualFreq() {
  bool ok = false;
  const double mhz = freq_edit_->text().toDouble(&ok);
  if (!ok || mhz <= 0.0) {
    flashStatus("频率输入无效（示例：121.500）", true);
    return;
  }
  const quint64 hz = static_cast<quint64>(mhz * 1e6 + 0.5);
  if (!session_.tuneTo(hz))
    flashStatus(QString("%1 MHz 超出接收范围").arg(mhz, 0, 'f', 3), true);
  else
    freq_edit_->clear();
}

void MainWindow::flashStatus(const QString& text, bool warn) {
  status_label_->setText(text);
  status_label_->setStyleSheet(
      warn ? "color: #ff5252; font-weight: bold;" : "color: #69f0ae;");
  status_timer_.start(3000);
}

void MainWindow::onBandClicked() {
  auto* btn = qobject_cast<QPushButton*>(sender());
  if (!btn) return;
  const auto hz = btn->property("center_hz").toULongLong();
  if (!session_.tuneTo(hz))
    flashStatus(QString("频段 %1 中心频率被拒绝").arg(btn->text()), true);
}

void MainWindow::onTuneStep(int direction) {
  qint64 step = 1'000;
  for (size_t i = 0; i < step_btns_.size(); ++i)
    if (step_btns_[i]->isChecked())
      step = step_btns_[i]->property("step_hz").toLongLong();
  session_.tuneBy(direction * step);
}

void MainWindow::onBenchTick() {
  synth_.fill(samples_.data());
  analyzer_.process(samples_.data(), samples_.size());

  // 1025 bins → 1024 显示列（相邻对取最大，保尖峰）
  const auto& db = analyzer_.powerDb();
  for (size_t i = 0; i < col_db_.size(); ++i)
    col_db_[i] = std::max(db[i * 2], db[i * 2 + 1]);

  wf_proc_.process(col_db_.data(), col_db_.size(), row_bytes_.data());
  waterfall_->pushRow(row_bytes_.data(), static_cast<int>(row_bytes_.size()));
  waterfall_->pushSpectrum(col_db_.data(), static_cast<int>(col_db_.size()));
}

#ifdef RADIO_SUITE_HAVE_LOGBOOK
void MainWindow::refreshQsoList() {
  if (!qso_list_) return;
  const auto qsos = hamlog_.recent(8);
  qso_list_->clear();
  for (const auto& q : qsos) {
    QListWidgetItem* it = new QListWidgetItem(QString("%1  %2 MHz  %3  %4-%5-%6 %7")
        .arg(q.callsign, -10)
        .arg(q.freq_mhz, 8, 'f', 4)
        .arg(q.mode, -4)
        .arg(q.year).arg(q.month, 2, 10, QChar('0')).arg(q.day, 2, 10, QChar('0'))
        .arg(q.time_hhmm));
    // QSL 卡债标色（hamlog_adapter 同语义：空=未发/未收）
    if (q.qsl_send.isEmpty()) it->setForeground(QColor(255, 160, 120));
    qso_list_->addItem(it);
  }
}

void MainWindow::onLogQso() {
  const QString call = qso_call_edit_->text().trimmed().toUpper();
  if (call.isEmpty()) {
    flashStatus("QSO 呼号为空", true);
    return;
  }
  const auto s = session_.store().snapshot();
  const QDateTime utc = QDateTime::currentDateTimeUtc();
  radio::logbook::Qso q;
  q.callsign = call;
  q.freq_mhz = static_cast<double>(s.freq_hz) / 1e6;
  q.year = utc.date().year();
  q.month = utc.date().month();
  q.day = utc.date().day();
  q.time_hhmm = utc.time().toString("hhmm");
  q.mode = radio::core::civ::modeName(s.mode);
  if (hamlog_.insert(q)) {
    flashStatus(QString("已写入 HamLog：%1 @ %2 MHz %3 %4Z")
                    .arg(call)
                    .arg(q.freq_mhz, 0, 'f', 4)
                    .arg(q.mode, q.time_hhmm),
                false);
    qso_call_edit_->clear();
    refreshQsoList();
  } else {
    flashStatus("HamLog 写入失败：" + hamlog_.lastError(), true);
  }
}

void MainWindow::onExportAdif() {
  const auto qsos = hamlog_.recent(1000);
  if (qsos.empty()) {
    flashStatus("HamLog 无记录可导出", true);
    return;
  }
  const QString path = QFileDialog::getSaveFileName(
      this, "ADIF 导出", "radio-suite.adi", "ADIF (*.adi)");
  if (path.isEmpty()) return;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    flashStatus("无法写文件：" + path, true);
    return;
  }
  const QString adif = radio::logbook::toAdif(qsos, hamlog_.myCallsign());
  f.write(adif.toUtf8());
  f.close();
  flashStatus(QString("已导出 %1 条 QSO → %2").arg(qsos.size()).arg(path), false);
}
#endif
