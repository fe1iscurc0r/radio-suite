// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/MainWindow.h"

#include "app/cw_keyer.h"
#include "app/memory_channel.h"
#include "app/repeater_dialog.h"
#include "app/set_mode_dialog.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>
#include <string>

#include "app/aprs_panel.h"
#include "app/connect_dialog.h"
#include "app/dial_widget.h"
#include "app/dtmf_dialog.h"
#include "app/meter_widgets.h"
#include "app/network_audio.h"
#include "app/sstv_dialog.h"
#include "app/voice_memory.h"
#include "app/wsjtx_panel.h"
#include "aprs/aprs.h"

#include <QComboBox>
#include <QListWidget>
#include <QRegularExpression>
#include <QSettings>

#ifdef RADIO_SUITE_HAVE_LOGBOOK
#include "app/wsjtx_client.h"
#include "app/wsjtx_logbook.h"
#include "logbook/hamlog_workbench.h"
#endif

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

QString stepCaption(qint64 hz) {
  return hz >= 1'000 ? QString("%1 kHz").arg(hz / 1'000)
                     : QString("%1 Hz").arg(hz);
}

}  // namespace

MainWindow::MainWindow(std::shared_ptr<radio::core::ICivTransport> transport,
                       QWidget* parent)
    : QMainWindow(parent), session_(std::move(transport)) {
  buildUi();

  samples_.assign(kFftSize, 0.0f);
  rx_ring_.assign(kFftSize, 0);
  col_db_.assign(static_cast<size_t>(WaterfallWidget::kTexW), -120.0f);
  row_bytes_.assign(static_cast<size_t>(WaterfallWidget::kTexW), 0);
  // 4 帧 Welch 功率平均：噪底抖动约减半，单音幅度不变（时间常数 ~0.26s）
  analyzer_.setAveragingFrames(4);

  // 50003 音频链：RX 播放 + PTT 门禁上行 + 真实频谱 tap
  network_audio_ = std::make_unique<radio::app::NetworkAudio>(&session_);
  connect(&session_, &radio::app::RadioSession::pttPhaseChanged, this,
          [this](int phase) {
            if (network_audio_)
              network_audio_->setTxActive(
                  phase == static_cast<int>(radio::core::PttPhase::Tx));
          });
  connect(network_audio_.get(), &radio::app::NetworkAudio::statusChanged, this,
          [this](const QString& text) { flashStatus(text, false); });
  network_audio_->setRxTap([this](std::span<const int16_t> pcm) {
    for (int16_t s : pcm) {
      rx_ring_[rx_ring_pos_] = s;
      rx_ring_pos_ = (rx_ring_pos_ + 1) % kFftSize;
    }
    rx_have_ = true;
    if (aprs_panel_) aprs_panel_->feedRxPcm(pcm);
  });

  // APRS：AFSK 走 50003 音频；RF 发射沿用一键发射的 PTT 编排
  aprs_panel_->setNetworkAudio(network_audio_.get());
  aprs_panel_->setTxController(
      [this] {
        if (!session_.beginScriptedTx()) return false;
        if (network_audio_) network_audio_->setTxActive(true);
        return true;
      },
      [this] {
        session_.endScriptedTx();
        if (network_audio_) network_audio_->setTxActive(false);
      });

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

MainWindow::~MainWindow() {
  // 先停音频（会发状态信号，此时成员仍存活），再让成员逆序析构
  if (network_audio_) network_audio_->stop();
}

void MainWindow::buildUi() {
  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);

  // 全局配色：原厂 Icom 遥控视觉语言（深灰炭黑 + 琥珀强调 + 激活背光）
  setStyleSheet(
      "QWidget { background: #1c1f24; color: #d8dde4; font-size: 10pt; }"
      "QLineEdit, QComboBox { background: #0d0f13; border: 1px solid #3a414c;"
      " border-radius: 5px; color: #ffb300; padding: 4px 8px; min-height: 20px; }"
      "QPushButton { background: #2b3038; border: 1px solid #444c58;"
      " border-radius: 6px; color: #cfd6df; padding: 6px 10px; min-height: 20px; }"
      "QPushButton:hover { background: #343b45; border-color: #5b6574; }"
      "QPushButton:pressed { background: #3d4652; }"
      "QPushButton:checked { background: #ffb300; color: #1a1a1a;"
      " font-weight: bold; border-color: #ffd54f; }"  // 原厂"指示灯点亮"背光态
      "QGroupBox { border: 1px solid #333a44; border-radius: 8px;"
      " margin-top: 10px; padding-top: 6px; }"
      "QGroupBox::title { subcontrol-origin: margin; left: 10px;"
      " color: #ffb300; padding: 0 4px; }"
      "QListWidget { background: #14171c; border: 1px solid #333a44; }"
      "QLabel { color: #aeb6c0; }"
      "QToolTip { background: #0d0f13; color: #ffb300; }"
      "QFrame#tuneDeck { background: #15181d; border: 1px solid #333a44;"
      " border-radius: 10px; }");

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
  mono.setPointSize(44);
  mono.setBold(true);
  freq_label_->setFont(mono);
  freq_label_->setStyleSheet("color: #ffb300; background: transparent;"
                             " qproperty-alignment: AlignCenter;");
  lcd_lay->addWidget(freq_label_, 1);
  mode_label_ = new QLabel("--", lcd);
  QFont moded = mode_label_->font();
  moded.setPointSize(17);
  moded.setBold(true);
  mode_label_->setFont(moded);
  mode_label_->setStyleSheet("color: #7ee0a0; background: transparent;");
  mode_label_->setAlignment(Qt::AlignCenter);
  mode_label_->setMinimumWidth(104);
  lcd_lay->addWidget(mode_label_);
  // 中继双频副显示：DUP± 时显示真实 TX 频率（offset 0x0C 读回，不猜惯例值）
  tx_label_ = new QLabel("", lcd);
  QFont txf = tx_label_->font();
  txf.setPointSize(18);
  txf.setBold(true);
  tx_label_->setFont(txf);
  tx_label_->setStyleSheet("color:#ff8a65; background: transparent;");
  tx_label_->setAlignment(Qt::AlignCenter);
  tx_label_->setMinimumWidth(250);
  lcd_lay->addWidget(tx_label_);
  top->addWidget(lcd, 1);

  ptt_btn_ = new QPushButton("PTT", central);
  ptt_btn_->setMinimumHeight(84);
  ptt_btn_->setMinimumWidth(220);
  QFont bold = ptt_btn_->font();
  bold.setBold(true);
  bold.setPointSize(15);
  ptt_btn_->setFont(bold);
  connect(ptt_btn_, &QPushButton::clicked, this,
          [this] { session_.pttButtonPressed(); });
  top->addWidget(ptt_btn_);

  // 连接设置（原厂 Connect Setting 件；表单生成命令行，重启生效）
  auto* conn_btn = new QPushButton("连接…", central);
  conn_btn->setMinimumSize(88, 40);
  connect(conn_btn, &QPushButton::clicked, this, [this] {
    ConnectDialog dlg(this);
    dlg.exec();
  });
  top->addWidget(conn_btn);
  root->addLayout(top, 0);

  // ---- 主体：左主列（仪表/调谐/功能/瀑布/日志） + 右竖大按键列 ----
  auto* body = new QHBoxLayout();
  auto* main_col = new QVBoxLayout();

  // 仪表 + 调谐同排：S 表靠左、旋钮居中、Po/SWR 缩到右侧
  main_col->addLayout(buildTuneBlock(central));
  main_col->addLayout(buildFuncBlock(central));

  waterfall_ = new WaterfallWidget(central);
  waterfall_->setDbRange(-110.0f, -10.0f);
  waterfall_->setMinimumHeight(260);
  waterfall_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  main_col->addWidget(waterfall_, 1);

#ifdef RADIO_SUITE_HAVE_LOGBOOK
  if (hamlog_.available()) {
    // 最近 QSO 与工作台入口同排，避免再吃掉一条频谱高度
    auto* qso_row = new QHBoxLayout();
    qso_row->setSpacing(6);
    qso_list_ = new QListWidget(central);
    qso_list_->setMaximumHeight(84);
    qso_row->addWidget(qso_list_, 1);
    refreshQsoList();

    auto* wb_btn = new QPushButton("HamLog\n工作台…", central);
    wb_btn->setMinimumWidth(108);
    wb_btn->setToolTip("C++ 重构的全表浏览/搜索/统计/ADIF 窗口（同一 Log.db）");
    connect(wb_btn, &QPushButton::clicked, this, [this] {
      auto* wb = new radio::logbook::HamLogWorkbench(this);
      wb->setAttribute(Qt::WA_DeleteOnClose);
      wb->resize(860, 520);
      wb->show();
    });
    qso_row->addWidget(wb_btn);
    main_col->addLayout(qso_row);
  }
#endif
  body->addLayout(main_col, 1);

  // ---- 右竖大按键列（显眼件：APRS / SSTV / 日志）----
  auto* side = new QVBoxLayout();

  // APRS 面板先建（下面 hamlog 块要用它 setLoginCall）
  // 独立窗口：地图 + 台站表 + 消息需要横向空间，右列放不下
  aprs_panel_ = new radio::app::AprsPanel(nullptr);
  aprs_panel_->setWindowFlag(Qt::Window);
  aprs_panel_->setWindowTitle("APRS 工作台 — radio-suite");
  aprs_panel_->resize(1080, 720);
  aprs_panel_->hide();
  connect(aprs_panel_, &radio::app::AprsPanel::statusMessage, this,
          [this](const QString& m) { flashStatus(m, false); });

  // WSJT-X 工作台：解码/QSO/WSPR 表 + Reply/Halt/FreeText 控制，独立窗口
  wsjtx_panel_ = new radio::app::WsjtxPanel(nullptr);
  wsjtx_panel_->setWindowFlag(Qt::Window);
  wsjtx_panel_->setWindowTitle("WSJT-X 接入 — radio-suite");
  wsjtx_panel_->resize(1000, 640);
  wsjtx_panel_->hide();
  connect(wsjtx_panel_, &radio::app::WsjtxPanel::statusMessage, this,
          [this](const QString& m, bool err) { flashStatus(m, err); });
  connect(wsjtx_panel_, &radio::app::WsjtxPanel::decodeActivated, this,
          [this](const QString& call, const QString& grid, quint64 hz,
                 const QString& mode) {
            if (qso_call_edit_) qso_call_edit_->setText(call);
            QString text = QString("WSJT-X 解码：%1").arg(call);
            if (hz != 0)
              text += QString(" · %1 MHz").arg(hz / 1e6, 0, 'f', 6);
            if (!mode.isEmpty()) text += " · " + mode;
            if (!grid.isEmpty()) text += " · " + grid;
            flashStatus(text, false);
          });

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
  aprs_big_btn_->setToolTip("APRS 工作台：地图/台站/消息/过滤器/RF+iGate");
  wsjtx_big_btn_ = mk_big("WSJT-X\n接入", QColor(0x1a, 0x23, 0x7e));
  wsjtx_big_btn_->setToolTip("WSJT-X UDP：解码/QSO/WSPR + Reply/Halt/FreeText");
  sstv_big_btn_ = mk_big("SSTV\n发送", QColor(0x8e, 0x24, 0xaa));
  sstv_big_btn_->setToolTip("慢扫描电视：图片→Robot36 音频（发送/接收解码）");
  dtmf_big_btn_ = mk_big("DTMF\n拨号", QColor(0x00, 0x69, 0x5c));
  dtmf_big_btn_->setToolTip("双音多频小键盘（ITU Q.23，生成 .wav）");
  auto* voice_big_btn_ = mk_big("VOICE\n录音", QColor(0x4a, 0x14, 0x8c));
  voice_big_btn_->setToolTip("Voice Memory：录音/列表/播放/门禁编排发射（原厂件）");
  auto* cw_big_btn_ = mk_big("CW\n键控", QColor(0x5d, 0x40, 0x37));
  cw_big_btn_->setToolTip("CW Keyer：0x17 文本发送 + 键速/侧音/半插入（原厂件）");
  auto* set_big_btn_ = mk_big("SET\n设置", QColor(0x37, 0x47, 0x4f));
  set_big_btn_->setToolTip("Set Mode 深设置树：0x1A 0x05 菜单项读写（原厂件）");
  auto* memch_big_btn_ = mk_big("MEM\n信道", QColor(0x00, 0x69, 0x5c));
  memch_big_btn_->setToolTip("记忆信道：0x1A 0x00 内容读写 + 0x08 组/信道选择（原厂件）");
  side->addWidget(aprs_big_btn_);
  side->addWidget(wsjtx_big_btn_);
  side->addWidget(sstv_big_btn_);
  side->addWidget(dtmf_big_btn_);
  side->addWidget(voice_big_btn_);
  side->addWidget(cw_big_btn_);
  side->addWidget(set_big_btn_);
  side->addWidget(memch_big_btn_);
  connect(sstv_big_btn_, &QPushButton::clicked, this, [this] {
    // 与主会话共享（真机时同一条链）；SstvDialog 改持裸指针+父窗口生命周期
    SstvDialog dlg(&session_, this);
    dlg.exec();
  });
  connect(dtmf_big_btn_, &QPushButton::clicked, this, [this] {
    DtmfDialog dlg(this);
    dlg.exec();
  });
  connect(voice_big_btn_, &QPushButton::clicked, this, [this] {
    VoiceMemoryDialog dlg(&session_, this);
    dlg.exec();
  });
  connect(cw_big_btn_, &QPushButton::clicked, this, [this] {
    radio::app::CwKeyerDialog dlg(&session_, this);
    dlg.exec();
  });
  connect(set_big_btn_, &QPushButton::clicked, this, [this] {
    radio::app::SetModeDialog dlg(&session_, this);
    dlg.exec();
  });
  connect(memch_big_btn_, &QPushButton::clicked, this, [this] {
    radio::app::MemoryChannelDialog dlg(&session_, this);
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
    {
      bool has = false;
      radio::aprs::LatLon pos;
      const QString grid = hamlog_.getSetting("my_grid");
      if (!grid.trimmed().isEmpty()) {
        bool ok = false;
        pos = radio::aprs::maidenheadToLatLon(grid.trimmed().toStdString(), &ok);
        has = ok;
      }
      if (!has) {
        const QString qth = hamlog_.getSetting("my_qth");
        const QStringList parts =
            qth.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
          bool ok1 = false, ok2 = false;
          const double lat = parts[0].toDouble(&ok1);
          const double lon = parts[1].toDouble(&ok2);
          if (ok1 && ok2) {
            pos = radio::aprs::LatLon{lat, lon};
            has = pos.valid();
          }
        }
      }
      aprs_panel_->setMyPosition(has, pos.lat, pos.lon);
      wsjtx_panel_->setMyGrid(grid.trimmed());
    }

    // WSJT-X 记完 QSO 直接落 HamLog（与手记 QSO 同一张 log 表）
    connect(wsjtx_panel_->client(), &radio::app::WsjtxClient::qsoLogged, this,
            [this](const radio::wsjtx::QsoLogged& logged, const QString&) {
              const auto record = radio::app::qsoFromWsjtx(logged);
              if (!hamlog_.insert(record)) {
                flashStatus("WSJT-X QSO 写入 HamLog 失败：" + hamlog_.lastError(),
                            true);
                return;
              }
              flashStatus(QString("WSJT-X QSO 已写入 HamLog：%1 %2 MHz %3")
                              .arg(record.callsign)
                              .arg(record.freq_mhz, 0, 'f', 4)
                              .arg(record.mode),
                          false);
              refreshQsoList();
            });
  }
#endif

  // APRS 面板挂右列（大按键切换显隐）
  aprs_big_btn_->setCheckable(true);
  connect(aprs_big_btn_, &QPushButton::toggled, this, [this](bool on) {
    aprs_panel_->setVisible(on);
    if (on) {
      aprs_panel_->raise();
      aprs_panel_->activateWindow();
      if (!aprs_panel_->isConnected()) aprs_panel_->toggleConnect();
    }
  });

  wsjtx_big_btn_->setCheckable(true);
  connect(wsjtx_big_btn_, &QPushButton::toggled, this, [this](bool on) {
    wsjtx_panel_->setVisible(on);
    if (on) {
      wsjtx_panel_->raise();
      wsjtx_panel_->activateWindow();
      if (!wsjtx_panel_->isListening()) wsjtx_panel_->startListening();
    }
  });
  side->addStretch(1);
  body->addLayout(side);
  root->addLayout(body, 1);

  status_label_ = new QLabel("transport: mock · radio: IC-705 sim", central);
  root->addWidget(status_label_);

  setCentralWidget(central);
  setMinimumSize(1180, 860);
  setWindowTitle("radio-suite — IC-705 控制台");
}

QVBoxLayout* MainWindow::buildTuneBlock(QWidget* central) {
  auto* block = new QVBoxLayout();
  block->setSpacing(6);

  // ---- 第一行：S 表（左）+ 紧凑旋钮台（中）+ Po/SWR（右）----
  auto* console_row = new QHBoxLayout();
  console_row->setSpacing(10);

  smeter_ = new SMeterWidget(central);
  console_row->addWidget(smeter_, 0, Qt::AlignVCenter);

  auto* deck = new QFrame(central);
  deck->setObjectName("tuneDeck");
  auto* deck_lay = new QHBoxLayout(deck);
  deck_lay->setContentsMargins(10, 6, 10, 6);
  deck_lay->setSpacing(12);

  dial_ = new DialWidget(deck);
  dial_->setFixedSize(132, 132);
  dial_->setToolTip("拖动或滚轮调谐：每格 = 当前步进档");
  connect(dial_, &DialWidget::steps, this, [this](int n) {
    qint64 step = 1'000;
    for (size_t i = 0; i < step_btns_.size(); ++i)
      if (step_btns_[i]->isChecked())
        step = step_btns_[i]->property("step_hz").toLongLong();
    session_.tuneBy(static_cast<int64_t>(step) * n);
  });
  deck_lay->addWidget(dial_, 0, Qt::AlignVCenter);

  auto* knob_col = new QVBoxLayout();
  knob_col->setSpacing(4);
  knob_col->addStretch(1);
  step_label_ = new QLabel(deck);
  step_label_->setStyleSheet("color: #ffb300; font-weight: bold;");
  knob_col->addWidget(step_label_);

  auto* step_row = new QHBoxLayout();
  step_row->setSpacing(4);
  for (int i = 0; i < 4; ++i) {
    auto* btn = new QPushButton(kStepNames[i], deck);
    btn->setCheckable(true);
    btn->setChecked(i == kDefaultStep);
    btn->setProperty("step_hz", static_cast<qlonglong>(kSteps[i]));
    btn->setMinimumSize(54, 28);
    const int idx = i;
    connect(btn, &QPushButton::clicked, this, [this, idx] {
      for (int j = 0; j < static_cast<int>(step_btns_.size()); ++j)
        step_btns_[static_cast<size_t>(j)]->setChecked(j == idx);
      step_label_->setText(
          QString("步进 %1（旋钮 / ▼▲ 每格）").arg(stepCaption(kSteps[idx])));
    });
    step_btns_.push_back(btn);
    step_row->addWidget(btn);
  }
  step_row->addStretch(1);
  knob_col->addLayout(step_row);

  auto* entry_row = new QHBoxLayout();
  entry_row->setSpacing(4);
  auto* down = new QPushButton("▼ 降频", deck);
  auto* up = new QPushButton("▲ 升频", deck);
  down->setMinimumSize(72, 30);
  up->setMinimumSize(72, 30);
  connect(down, &QPushButton::clicked, this, [this] { onTuneStep(-1); });
  connect(up, &QPushButton::clicked, this, [this] { onTuneStep(+1); });
  entry_row->addWidget(down);
  entry_row->addWidget(up);
  freq_edit_ = new QLineEdit(deck);
  freq_edit_->setPlaceholderText("直接输入 MHz，如 145.500");
  freq_edit_->setMinimumHeight(30);
  freq_edit_->setAlignment(Qt::AlignRight);
  connect(freq_edit_, &QLineEdit::returnPressed, this, &MainWindow::onManualFreq);
  entry_row->addWidget(freq_edit_, 1);
  auto* go = new QPushButton("GO", deck);
  go->setMinimumSize(48, 30);
  connect(go, &QPushButton::clicked, this, &MainWindow::onManualFreq);
  entry_row->addWidget(go);
  knob_col->addLayout(entry_row);
  knob_col->addStretch(1);
  deck_lay->addLayout(knob_col, 1);
  console_row->addWidget(deck, 1);

  poswr_ = new PoSwrWidget(central);
  console_row->addWidget(poswr_, 0, Qt::AlignVCenter);

  block->addLayout(console_row);

  // ---- 频段快捷 + 接收波段（可听不可发）----
  auto* bands = new QHBoxLayout();
  bands->setSpacing(5);
  for (const auto& b : kBands) {
    auto* btn = new QPushButton(b.name, central);
    btn->setMinimumSize(54, 28);
    btn->setProperty("center_hz", static_cast<qulonglong>(b.center_hz));
    connect(btn, &QPushButton::clicked, this, &MainWindow::onBandClicked);
    bands->addWidget(btn);
  }
  bands->addStretch(1);
  auto mk_preset = [&](const QString& text, const QString& tip, auto action) {
    auto* btn = new QPushButton(text, central);
    btn->setToolTip(tip);
    btn->setMinimumSize(64, 28);
    connect(btn, &QPushButton::clicked, this, action);
    bands->addWidget(btn);
  };
  mk_preset("AIR", "民航话音 118-137（AM，中点 127.5，可听不可发）", [this] {
    session_.setMode(radio::core::civ::Mode::AM);
    session_.tuneTo(127'500'000);
  });
  mk_preset("BC-FM", "调频广播 87.5-108（WFM，中点 97.75，可听不可发）", [this] {
    session_.setMode(radio::core::civ::Mode::WFM);
    session_.tuneTo(97'750'000);
  });
  mk_preset("BC-SW", "短波广播 31m（AM，9.5，可听不可发）", [this] {
    session_.setMode(radio::core::civ::Mode::AM);
    session_.tuneTo(9'500'000);
  });
  block->addLayout(bands);

  // ---- 双 VFO（SUB 带）----
  auto* vfo = new QHBoxLayout();
  vfo->setSpacing(5);
  vfo->addWidget(new QLabel("双 VFO", central));
  sub_label_ = new QLabel("—", central);
  sub_label_->setMinimumWidth(190);
  QFont subf("Cascadia Mono");
  subf.setStyleHint(QFont::Monospace);
  subf.setPointSize(11);
  sub_label_->setFont(subf);
  sub_label_->setStyleSheet("color: #4dd0e1;");
  vfo->addWidget(sub_label_);

  auto mk_vfo = [&](const QString& text, const QString& tip) {
    auto* b = new QPushButton(text, central);
    b->setToolTip(tip);
    b->setMinimumSize(48, 28);
    vfo->addWidget(b);
    return b;
  };
  vfo_a_btn_ = mk_vfo("A 主", "选 MAIN（A）");
  vfo_b_btn_ = mk_vfo("B 副", "选 SUB（B）");
  vfo_swap_btn_ = mk_vfo("A⇄B", "交换两个 VFO（发射中拒绝）");
  vfo_equal_btn_ = mk_vfo("A=B", "把当前 VFO 复制到另一 VFO（发射中拒绝）");
  vfo_a_btn_->setCheckable(true);
  vfo_b_btn_->setCheckable(true);
  connect(vfo_a_btn_, &QPushButton::clicked, this, [this] {
    if (!session_.selectVfo(false)) flashStatus("发射中禁止切换 VFO", true);
  });
  connect(vfo_b_btn_, &QPushButton::clicked, this, [this] {
    if (!session_.selectVfo(true)) flashStatus("发射中禁止切换 VFO", true);
  });
  connect(vfo_swap_btn_, &QPushButton::clicked, this, [this] {
    if (session_.swapVfo()) flashStatus("A⇄B 已交换", false);
    else flashStatus("发射中禁止交换 VFO", true);
  });
  connect(vfo_equal_btn_, &QPushButton::clicked, this, [this] {
    if (session_.equalizeVfo()) flashStatus("当前 VFO 已复制到另一 VFO", false);
    else flashStatus("发射中禁止 VFO 复制", true);
  });

  // 双守听：IC-705 单接收机，不摆假按钮；如实说明替代方案
  auto* dual_info = new QPushButton("双守听？", central);
  dual_info->setToolTip("IC-705 无 DUAL 双守听：单接收机。用 A/B 双 VFO 手动守听 SUB 带。");
  dual_info->setMinimumSize(84, 28);
  dual_info->setStyleSheet(
      "QPushButton { border-color: #546e7a; color: #90caf9; }");
  connect(dual_info, &QPushButton::clicked, this, [this] {
    QMessageBox::information(
        this, "双守听 / Dual Watch",
        "IC-705 不支持双守听（Dual Watch）：\n"
        "· 出厂 IC-705.ini 的 DUAL / DUAL_KIND 均为 0；\n"
        "· CI-V 0x07 只有 MAIN/SUB 选择，没有 DUAL 开关；\n"
        "· 上游 ic705.toml 也描述为单接收机。\n\n"
        "本软件提供 A/B 双 VFO 手动守听作为替代：\n"
        "· A/B 选择 MAIN 或 SUB 带，A⇄B 交换，A=B 对齐；\n"
        "· SUB 带频率/模式实时显示在调谐台下方。\n\n"
        "不摆放无效按钮：若未来机型/固件支持 DUAL，再按回读驱动接入。");
  });
  vfo->addWidget(dual_info);
  vfo->addStretch(1);
  block->addLayout(vfo);

  step_label_->setText(
      QString("步进 %1（旋钮 / ▼▲ 每格）").arg(stepCaption(kSteps[kDefaultStep])));
  return block;
}

QVBoxLayout* MainWindow::buildFuncBlock(QWidget* central) {
  auto* block = new QVBoxLayout();
  block->setSpacing(8);

  // 行 1：模式组（回读驱动）+ 中继频差方向 + 中继/亚音对话框
  auto* modes = new QHBoxLayout();
  modes->setSpacing(6);
  const std::pair<const char*, radio::core::civ::Mode> kModes[] = {
      {"LSB", radio::core::civ::Mode::LSB}, {"USB", radio::core::civ::Mode::USB},
      {"AM", radio::core::civ::Mode::AM},   {"CW", radio::core::civ::Mode::CW},
      {"FM", radio::core::civ::Mode::FM},   {"WFM", radio::core::civ::Mode::WFM}};
  for (const auto& [name, m] : kModes) {
    auto* btn = new QPushButton(name, central);
    btn->setCheckable(true);
    btn->setMinimumSize(56, 34);
    const auto mode = m;
    connect(btn, &QPushButton::clicked, this, [this, btn, mode] {
      session_.setMode(mode);
      btn->setChecked(true);  // 乐观更新，250ms 回读纠偏
    });
    mode_btns_.push_back(btn);
    modes->addWidget(btn);
  }
  modes->addSpacing(12);
  dup_btn_ = new QPushButton("SPLX", central);
  dup_btn_->setToolTip("中继频差方向：SPLX → DUP− → DUP+（0x0F）");
  dup_btn_->setMinimumSize(72, 34);
  connect(dup_btn_, &QPushButton::clicked, this, [this] {
    const int cur = session_.store().snapshot().duplex_raw;
    const uint8_t next = cur == 0x00 || cur == 0x10 ? 0x11 : (cur == 0x11 ? 0x12 : 0x10);
    session_.setDuplex(next);
  });
  modes->addWidget(dup_btn_);
  repeater_btn_ = new QPushButton("中继/亚音…", central);
  repeater_btn_->setToolTip(
      "频差 0x0D + 模拟亚音 CTCSS/TSQL + 数字亚音 DTCS（含 RX/TX 双频显示）");
  repeater_btn_->setMinimumSize(150, 34);
  repeater_btn_->setStyleSheet(
      "QPushButton { background: #3a2a20; border-color: #8d6e63;"
      " color: #ffcc80; font-weight: bold; }");
  connect(repeater_btn_, &QPushButton::clicked, this, [this] {
    radio::app::RepeaterDialog dlg(&session_, this);
    dlg.exec();
  });
  modes->addWidget(repeater_btn_);
  modes->addStretch(1);
  block->addLayout(modes);

  // 行 2：功能开关（回读驱动）+ RIT/ΔTX/SPLIT + 本地记忆
  auto* funcs = new QHBoxLayout();
  funcs->setSpacing(6);
  auto mk_toggle = [&](const QString& name, const QString& tip, auto setter) {
    auto* btn = new QPushButton(name, central);
    btn->setCheckable(true);
    btn->setToolTip(tip);
    btn->setMinimumSize(56, 32);
    connect(btn, &QPushButton::clicked, this, [btn, setter] { setter(btn->isChecked()); });
    funcs->addWidget(btn);
    return btn;
  };
  att_btn_ = mk_toggle("ATT", "20dB 衰减器（电台仅 HF/50MHz 生效）",
                       [this](bool on) { session_.setAtt(on); });
  nb_btn_ = mk_toggle("NB", "噪声消隐 0x16", [this](bool on) {
    session_.setSw16(radio::core::civ::kSub16Nb, on ? 1 : 0);
  });
  nr_btn_ = mk_toggle("NR", "噪声抑制 0x16", [this](bool on) {
    session_.setSw16(radio::core::civ::kSub16Nr, on ? 1 : 0);
  });
  moni_btn_ = mk_toggle("MONI", "发射监听 0x16", [this](bool on) {
    session_.setSw16(radio::core::civ::kSub16Moni, on ? 1 : 0);
  });
  notch_btn_ = mk_toggle("NOTCH", "自动陷波 0x16", [this](bool on) {
    session_.setSw16(radio::core::civ::kSub16NotchAuto, on ? 1 : 0);
  });
  vox_btn_ = mk_toggle("VOX", "声控发射 0x16", [this](bool on) {
    session_.setSw16(radio::core::civ::kSub16Vox, on ? 1 : 0);
  });
  pamp_btn_ = new QPushButton("PAMP", central);
  pamp_btn_->setToolTip("前置放大器循环：OFF → P1 → P2");
  pamp_btn_->setMinimumSize(88, 32);
  connect(pamp_btn_, &QPushButton::clicked, this, [this] {
    const int cur = session_.store().snapshot().pamp;
    session_.setSw16(radio::core::civ::kSub16Pamp,
                     static_cast<uint8_t>((cur + 1) % 3));
  });
  funcs->addWidget(pamp_btn_);
  agc_btn_ = new QPushButton("AGC", central);
  agc_btn_->setToolTip("AGC 循环：F → M → S");
  agc_btn_->setMinimumSize(74, 32);
  connect(agc_btn_, &QPushButton::clicked, this, [this] {
    const int cur = session_.store().snapshot().agc;
    session_.setSw16(radio::core::civ::kSub16Agc,
                     static_cast<uint8_t>(cur >= 3 ? 1 : cur + 1));
  });
  funcs->addWidget(agc_btn_);

  funcs->addSpacing(12);
  auto mk_rit_toggle = [&](const QString& name, const QString& tip, auto setter) {
    auto* btn = new QPushButton(name, central);
    btn->setCheckable(true);
    btn->setToolTip(tip);
    btn->setMinimumSize(58, 32);
    connect(btn, &QPushButton::clicked, this, [btn, setter] { setter(btn->isChecked()); });
    funcs->addWidget(btn);
    return btn;
  };
  rit_btn_ = mk_rit_toggle("RIT", "接收增量调谐 0x21",
                           [this](bool on) { session_.setRit(on, false); });
  dtx_btn_ = mk_rit_toggle("ΔTX", "发射增量调谐 0x21",
                           [this](bool on) { session_.setRit(on, true); });
  split_btn_ = mk_rit_toggle("SPLIT", "异频收发：发射频率 = 另一 VFO", [this](bool on) {
    flashStatus(on ? "Split 已开：发射频率=另一 VFO，注意合规" : "Split 关", !on);
    session_.setSplit(on);
  });

  // 本地记忆（电台内存信道 M3 接；本地槽即刻可用）
  funcs->addSpacing(12);
  mem_slot_ = new QComboBox(central);
  mem_slot_->setMinimumSize(76, 32);
  for (int i = 1; i <= 8; ++i) mem_slot_->addItem(QString("M%1").arg(i));
  funcs->addWidget(mem_slot_);
  auto* mem_save = new QPushButton("存", central);
  mem_save->setToolTip("当前频率/模式存入所选槽（本地）");
  mem_save->setMinimumSize(48, 32);
  connect(mem_save, &QPushButton::clicked, this, &MainWindow::onMemSave);
  funcs->addWidget(mem_save);
  auto* mem_recall = new QPushButton("取", central);
  mem_recall->setToolTip("取出所选槽的频率/模式");
  mem_recall->setMinimumSize(48, 32);
  connect(mem_recall, &QPushButton::clicked, this, &MainWindow::onMemRecall);
  funcs->addWidget(mem_recall);

  // 网络音频（RS-BA1 50003）：RX→扬声器、麦克风→PTT 上行
  funcs->addSpacing(12);
  audio_btn_ = new QPushButton("网络音频", central);
  audio_btn_->setCheckable(true);
  audio_btn_->setMinimumSize(96, 32);
  const bool has_audio = session_.audioLink() != nullptr;
  audio_btn_->setEnabled(has_audio);
  audio_btn_->setToolTip(
      has_audio
          ? "RS-BA1 50003：RX 下行→扬声器；麦克风→PTT 上行（默认设备，48kHz s16le）"
          : "当前链路无 50003 音频（mock / Python 桥）；用 --radio <IP> 连真机后可用");
  audio_btn_->setStyleSheet(
      "QPushButton { background:#12333a; border-color:#2e7d8a; color:#8fe; }"
      "QPushButton:checked { background:#00acc1; color:#06131a;"
      " font-weight:bold; border-color:#4dd0e1; }");
  connect(audio_btn_, &QPushButton::toggled, this, [this](bool on) {
    if (on) {
      std::string err;
      if (!network_audio_ || !network_audio_->start(&err)) {
        QSignalBlocker guard(audio_btn_);
        audio_btn_->setChecked(false);
        flashStatus(
            QString("网络音频未开：%1").arg(QString::fromStdString(err)), true);
      }
    } else if (network_audio_) {
      network_audio_->stop();
    }
  });
  funcs->addWidget(audio_btn_);
  funcs->addStretch(1);
  block->addLayout(funcs);
  return block;
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
  // 中继入口按钮文案随回读更新（频差方向 + 亚音模式）
  QString rpt = "中继/亚音…";
  if (s.duplex_raw == 0x11) rpt = "中继 DUP−…";
  else if (s.duplex_raw == 0x12) rpt = "中继 DUP+…";
  if (s.tone_squelch != 0) {
    const auto tone = radio::core::civ::toneSquelchFromCode(
        static_cast<uint8_t>(s.tone_squelch));
    if (tone) rpt += QString(" [%1]").arg(radio::core::civ::toneSquelchName(*tone));
  }
  repeater_btn_->setText(rpt);
  const auto rb = [&s](QPushButton* b, bool on) { QSignalBlocker g(b); b->setChecked(on); };
  rb(rit_btn_, s.rit);
  rb(dtx_btn_, s.dtx);
  rb(split_btn_, s.split);
  {
    QSignalBlocker ga(vfo_a_btn_);
    QSignalBlocker gb(vfo_b_btn_);
    vfo_a_btn_->setChecked(!s.selected_sub);
    vfo_b_btn_->setChecked(s.selected_sub);
  }
}

void MainWindow::refreshState() {
  const auto s = session_.store().snapshot();
  freq_label_->setText(formatFreq(s.freq_hz));
  mode_label_->setText(QString("%1/%2").arg(radio::core::civ::modeName(s.mode)).arg(s.filter));
  smeter_->setDbm(s.smeter_dbm);
  poswr_->setPoSwr(s.po_w, s.swr);
  // 中继双频：DUP−→TX=RX−offset，DUP+→TX=RX+offset（offset 电台回读）
  const uint64_t tx_hz = radio::core::effectiveTxHz(s);
  if (s.duplex_raw == 0x11 || s.duplex_raw == 0x12) {
    tx_label_->setText(QString("TX %1 %2 MHz")
                           .arg(s.duplex_raw == 0x11 ? "▼" : "▲")
                           .arg(tx_hz / 1e6, 0, 'f', 6));
    tx_label_->setToolTip(QString("中继双频：接收 %1 MHz（显示），发射 %2 MHz（DUP%3，频差 %4 kHz）")
                              .arg(s.freq_hz / 1e6, 0, 'f', 6)
                              .arg(tx_hz / 1e6, 0, 'f', 6)
                              .arg(s.duplex_raw == 0x11 ? "−" : "+")
                              .arg(s.offset_hz / 1000.0, 0, 'f', 1));
  } else {
    tx_label_->setText(QString("TX = %1 MHz").arg(tx_hz / 1e6, 0, 'f', 6));
    tx_label_->setToolTip("同频（simplex）：收发同频");
  }
  sub_label_->setText(QString("SUB %1 %2")
                          .arg(formatFreq(s.sub_freq_hz))
                          .arg(radio::core::civ::modeName(s.sub_mode)));
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
  // 网络音频开启且已收到下行：频谱直接用真实 RX PCM；否则回落到合成谱
  const bool live = network_audio_ && network_audio_->active() && rx_have_;
  if (live) {
    for (size_t i = 0; i < kFftSize; ++i)
      samples_[i] = rx_ring_[(rx_ring_pos_ + i) % kFftSize] / 32768.0f;
  } else {
    synth_.fill(samples_.data());
  }
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
  // 中继通联：备注写入上下行双频（HamLog 表 Freq 按惯例记下行/接收频率）
  if (s.duplex_raw == 0x11 || s.duplex_raw == 0x12) {
    const uint64_t tx = radio::core::effectiveTxHz(s);
    q.remarks = QString("中继 DUP%1：下行 %2 / 上行 TX %3 MHz")
                    .arg(s.duplex_raw == 0x11 ? "−" : "+")
                    .arg(s.freq_hz / 1e6, 0, 'f', 6)
                    .arg(tx / 1e6, 0, 'f', 6);
  }
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
