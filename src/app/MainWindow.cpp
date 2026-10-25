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
#include "app/connect_dialog.h"
#include "app/dial_widget.h"
#include "app/meter_widgets.h"
#include "app/sstv_dialog.h"

#include <QComboBox>
#include <QListWidget>
#include <QSettings>

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

  // 全局配色：原厂 Icom 遥控视觉语言（深灰炭黑 + 琥珀强调 + 激活背光）
  setStyleSheet(
      "QWidget { background: #1c1f24; color: #d8dde4; font-size: 10pt; }"
      "QLineEdit, QComboBox { background: #0d0f13; border: 1px solid #3a414c;"
      " color: #ffb300; padding: 2px 6px; }"
      "QPushButton { background: #2b3038; border: 1px solid #444c58;"
      " color: #cfd6df; padding: 4px 8px; }"
      "QPushButton:hover { background: #343b45; }"
      "QPushButton:checked { background: #ffb300; color: #1a1a1a;"
      " font-weight: bold; }"  // 原厂"指示灯点亮"背光态
      "QListWidget { background: #14171c; border: 1px solid #333a44; }"
      "QLabel { color: #aeb6c0; }"
      "QToolTip { background: #0d0f13; color: #ffb300; }");

  // ---- 顶栏（主）：LCD 风频率带（原厂琥珀屏）+ PTT + 连接 ----
  auto* top = new QHBoxLayout();
  auto* lcd = new QFrame(central);
  lcd->setObjectName("lcdBand");
  lcd->setStyleSheet(
      "QFrame#lcdBand { background: #060804; border: 2px solid #2c313a;"
      " border-radius: 4px; }");
  auto* lcd_lay = new QHBoxLayout(lcd);
  lcd_lay->setContentsMargins(16, 6, 16, 6);
  freq_label_ = new QLabel("__.__._ MHz", lcd);
  QFont mono("Cascadia Mono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPointSize(36);
  mono.setBold(true);
  freq_label_->setFont(mono);
  freq_label_->setStyleSheet("color: #ffb300; background: transparent;"
                             " qproperty-alignment: AlignCenter;");
  lcd_lay->addWidget(freq_label_, 1);
  mode_label_ = new QLabel("--", lcd);
  QFont moded = mode_label_->font();
  moded.setPointSize(15);
  moded.setBold(true);
  mode_label_->setFont(moded);
  mode_label_->setStyleSheet("color: #7ee0a0; background: transparent;");
  mode_label_->setAlignment(Qt::AlignCenter);
  mode_label_->setMinimumWidth(96);
  lcd_lay->addWidget(mode_label_);
  top->addWidget(lcd, 1);

  ptt_btn_ = new QPushButton("PTT", central);
  ptt_btn_->setMinimumHeight(72);
  ptt_btn_->setMinimumWidth(210);
  QFont bold = ptt_btn_->font();
  bold.setBold(true);
  bold.setPointSize(14);
  ptt_btn_->setFont(bold);
  connect(ptt_btn_, &QPushButton::clicked, this,
          [this] { session_.pttButtonPressed(); });
  top->addWidget(ptt_btn_);

  // 连接设置（原厂 Connect Setting 件；表单生成命令行，重启生效）
  auto* conn_btn = new QPushButton("连接…", central);
  connect(conn_btn, &QPushButton::clicked, this, [this] {
    ConnectDialog dlg(this);
    dlg.exec();
  });
  top->addWidget(conn_btn);
  root->addLayout(top, 0);

  // ---- 主体：左主列（仪表/调谐/功能/瀑布/日志） + 右竖大按键列 ----
  auto* body = new QHBoxLayout();
  auto* main_col = new QVBoxLayout();

  // 仪表行（主信息）：指针 S 表 + Po/SWR
  auto* meters = new QHBoxLayout();
  smeter_ = new SMeterWidget(central);
  poswr_ = new PoSwrWidget(central);
  meters->addWidget(smeter_, 3);
  meters->addWidget(poswr_, 2);

  // 主调谐旋钮（原厂 M-Tune 件；拖拽/滚轮按当前步进档步进）
  dial_ = new DialWidget(central);
  connect(dial_, &DialWidget::steps, this, [this](int n) {
    qint64 step = 1'000;
    for (size_t i = 0; i < step_btns_.size(); ++i)
      if (step_btns_[i]->isChecked())
        step = step_btns_[i]->property("step_hz").toLongLong();
    session_.tuneBy(static_cast<int64_t>(step) * n);
  });
  meters->addWidget(dial_);
  main_col->addLayout(meters);
  main_col->addLayout(buildTuneRow(central));
  main_col->addLayout(buildFuncRow(central));

  waterfall_ = new WaterfallWidget(central);
  waterfall_->setDbRange(-110.0f, -10.0f);
  main_col->addWidget(waterfall_, 1);

#ifdef RADIO_SUITE_HAVE_LOGBOOK
  if (hamlog_.available()) {
    qso_list_ = new QListWidget(central);
    qso_list_->setMaximumHeight(96);
    main_col->addWidget(qso_list_);
    refreshQsoList();
  }
#endif
  body->addLayout(main_col, 1);

  // ---- 右竖大按键列（显眼件：APRS / SSTV / 日志）----
  auto* side = new QVBoxLayout();

  // APRS 面板先建（下面 hamlog 块要用它 setLoginCall）
  aprs_panel_ = new radio::app::AprsPanel(central);
  aprs_panel_->hide();
  connect(aprs_panel_, &radio::app::AprsPanel::statusMessage, this,
          [this](const QString& m) { flashStatus(m, false); });

  auto mk_big = [central](const QString& text, const QColor& c) {
    auto* b = new QPushButton(text, central);
    b->setMinimumHeight(64);
    b->setMinimumWidth(130);
    QFont f = b->font();
    f.setPointSize(12);
    f.setBold(true);
    b->setFont(f);
    b->setStyleSheet(QString("QPushButton { background: %1; color: white; }")
                         .arg(c.name()));
    return b;
  };
  aprs_big_btn_ = mk_big("APRS\n台流", QColor(0x00, 0x69, 0x7c));
  sstv_big_btn_ = mk_big("SSTV\n发送", QColor(0x8e, 0x24, 0xaa));
  side->addWidget(aprs_big_btn_);
  side->addWidget(sstv_big_btn_);
  connect(sstv_big_btn_, &QPushButton::clicked, this, [this] {
    SstvDialog dlg(this);
    dlg.exec();
  });

#ifdef RADIO_SUITE_HAVE_LOGBOOK
  if (hamlog_.available()) {
    qso_call_edit_ = new QLineEdit(central);
    qso_call_edit_->setPlaceholderText("对方呼号");
    side->addWidget(qso_call_edit_);
    auto* log_btn = mk_big("记 QSO", QColor(0x2e, 0x7d, 0x32));
    log_btn->setMinimumHeight(48);
    side->addWidget(log_btn);
    connect(log_btn, &QPushButton::clicked, this, &MainWindow::onLogQso);
    auto* adif_btn = mk_big("ADIF 导出", QColor(0x37, 0x47, 0x4f));
    adif_btn->setMinimumHeight(40);
    side->addWidget(adif_btn);
    connect(adif_btn, &QPushButton::clicked, this, &MainWindow::onExportAdif);
    aprs_panel_->setLoginCall(hamlog_.myCallsign());
  }
#endif

  // APRS 面板挂右列（大按键切换显隐）
  aprs_big_btn_->setCheckable(true);
  connect(aprs_big_btn_, &QPushButton::toggled, this, [this](bool on) {
    aprs_panel_->setVisible(on);
    if (on && !aprs_panel_->isConnected()) aprs_panel_->toggleConnect();
  });
  side->addWidget(aprs_panel_);
  side->addStretch(1);
  body->addLayout(side);
  root->addLayout(body, 1);

  status_label_ = new QLabel("transport: mock · radio: IC-705 sim", central);
  root->addWidget(status_label_);

  setCentralWidget(central);
  setWindowTitle("radio-suite — M0 spike");
}

QHBoxLayout* MainWindow::buildTuneRow(QWidget* central) {
  // 频段快捷 / 步进档（互斥按钮组）/ 升降 / 手动输频 / 接收波段
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
  down->setMinimumWidth(44);
  up->setMinimumWidth(44);
  connect(down, &QPushButton::clicked, this, [this] { onTuneStep(-1); });
  connect(up, &QPushButton::clicked, this, [this] { onTuneStep(+1); });
  tune->addWidget(down);
  tune->addWidget(up);

  freq_edit_ = new QLineEdit(central);
  freq_edit_->setPlaceholderText("MHz，如 127.500");
  freq_edit_->setFixedWidth(110);
  freq_edit_->setAlignment(Qt::AlignRight);
  connect(freq_edit_, &QLineEdit::returnPressed, this, &MainWindow::onManualFreq);
  tune->addWidget(freq_edit_);
  auto* go = new QPushButton("GO", central);
  go->setMinimumWidth(40);
  connect(go, &QPushButton::clicked, this, &MainWindow::onManualFreq);
  tune->addWidget(go);
  auto* air = new QPushButton("AIR", central);
  air->setToolTip("民航话音 118-137（AM，中点 127.5，可听不可发）");
  connect(air, &QPushButton::clicked, this, [this] {
    session_.setMode(radio::core::civ::Mode::AM);
    session_.tuneTo(127'500'000);
  });
  tune->addWidget(air);
  auto* bc = new QPushButton("BC-FM", central);
  bc->setToolTip("调频广播 87.5-108（WFM，中点 97.75，可听不可发）");
  connect(bc, &QPushButton::clicked, this, [this] {
    session_.setMode(radio::core::civ::Mode::WFM);
    session_.tuneTo(97'750'000);
  });
  tune->addWidget(bc);
  auto* bcsw = new QPushButton("BC-SW", central);
  bcsw->setToolTip("短波广播 31m（AM，9.5，可听不可发）");
  connect(bcsw, &QPushButton::clicked, this, [this] {
    session_.setMode(radio::core::civ::Mode::AM);
    session_.tuneTo(9'500'000);
  });
  tune->addWidget(bcsw);
  return tune;
}

QHBoxLayout* MainWindow::buildFuncRow(QWidget* central) {
  // 模式组（回读驱动）+ ATT/NB/NR/MONI/NOTCH/VOX/PAMP/AGC/DUP（紧凑次级行）
  auto* funcs = new QHBoxLayout();
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
  att_btn_ = mk_toggle("ATT", [this](bool on) { session_.setAtt(on); });
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
    const int cur = session_.store().snapshot().duplex_raw;
    const uint8_t next = cur == 0x00 || cur == 0x10 ? 0x11 : (cur == 0x11 ? 0x12 : 0x10);
    session_.setDuplex(next);
  });
  funcs->addWidget(dup_btn_);

  // RIT / ΔTX / Split（原厂件，0x21 族 / 0x0F split 值域）
  auto mk_rit_toggle = [&](const QString& name, auto setter) {
    auto* btn = new QPushButton(name, central);
    btn->setCheckable(true);
    connect(btn, &QPushButton::clicked, this, [btn, setter] { setter(btn->isChecked()); });
    funcs->addWidget(btn);
    return btn;
  };
  rit_btn_ = mk_rit_toggle("RIT", [this](bool on) { session_.setRit(on, false); });
  dtx_btn_ = mk_rit_toggle("ΔTX", [this](bool on) { session_.setRit(on, true); });
  split_btn_ = mk_rit_toggle("SPLIT", [this](bool on) {
    flashStatus(on ? "Split 已开：发射频率=另一 VFO，注意合规" : "Split 关", !on);
    session_.setSplit(on);
  });

  // 本地记忆（电台内存信道 M3 接；本地槽即刻可用）
  mem_slot_ = new QComboBox(central);
  for (int i = 1; i <= 8; ++i) mem_slot_->addItem(QString("M%1").arg(i));
  funcs->addWidget(mem_slot_);
  auto* mem_save = new QPushButton("存", central);
  mem_save->setToolTip("当前频率/模式存入所选槽（本地）");
  connect(mem_save, &QPushButton::clicked, this, &MainWindow::onMemSave);
  funcs->addWidget(mem_save);
  auto* mem_recall = new QPushButton("取", central);
  mem_recall->setToolTip("取出所选槽的频率/模式");
  connect(mem_recall, &QPushButton::clicked, this, &MainWindow::onMemRecall);
  funcs->addWidget(mem_recall);
  return funcs;
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
  const auto rb = [&s](QPushButton* b, bool on) { QSignalBlocker g(b); b->setChecked(on); };
  rb(rit_btn_, s.rit);
  rb(dtx_btn_, s.dtx);
  rb(split_btn_, s.split);
}

void MainWindow::refreshState() {
  const auto s = session_.store().snapshot();
  freq_label_->setText(formatFreq(s.freq_hz));
  mode_label_->setText(QString("%1/%2").arg(radio::core::civ::modeName(s.mode)).arg(s.filter));
  smeter_->setDbm(s.smeter_dbm);
  poswr_->setPoSwr(s.po_w, s.swr);
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

void MainWindow::onMemSave() {
  const auto s = session_.store().snapshot();
  const QString key = QString("mem/%1").arg(mem_slot_->currentText());
  QSettings st;
  st.setValue(key + "/freq", qulonglong(s.freq_hz));
  st.setValue(key + "/mode", int(s.mode));
  flashStatus(QString("%1 已存 %2 MHz %3")
                  .arg(mem_slot_->currentText())
                  .arg(s.freq_hz / 1e6, 0, 'f', 4)
                  .arg(radio::core::civ::modeName(s.mode)),
              false);
}

void MainWindow::onMemRecall() {
  const QString key = QString("mem/%1").arg(mem_slot_->currentText());
  QSettings st;
  const auto freq = st.value(key + "/freq").toULongLong();
  const int mode = st.value(key + "/mode").toInt();
  if (freq == 0) {
    flashStatus(QString("%1 槽为空").arg(mem_slot_->currentText()), true);
    return;
  }
  session_.setMode(static_cast<radio::core::civ::Mode>(mode));
  session_.tuneTo(freq);
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
