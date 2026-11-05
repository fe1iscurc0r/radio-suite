#include "app/MainWindow.h"
// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/MainWindow_panels.cpp — MainWindow 桌面 UI 构建域（同一类的第二实现文件）：
// 连接对话框 / 桌面一屏两列布局 / APRS·WSJT-X·FT8 面板对象与接线 / LCD 频率带 / 大按键列 / HamLog 融合区。

#include "app/aprs_panel.h"
#include "app/connect_dialog.h"
#include "app/cw_keyer.h"
#include "app/dtmf_dialog.h"
#include "app/ft8_panel.h"
#include "app/memory_channel.h"
#include "app/meter_widgets.h"
#include "app/network_audio.h"
#include "app/self_check_dialog.h"
#include "app/set_mode_dialog.h"
#include "app/sstv_dialog.h"
#include "app/touch_dialog.h"
#include "app/voice_memory.h"
#include "app/wsjtx_panel.h"
#include "aprs/aprs.h"
#include "audio/audio_in.h"
#include "audio/audio_out.h"
#include "ui_common/app_settings.h"

#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QRegularExpression>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#ifdef Q_OS_ANDROID
#include <QGuiApplication>
#include <QScreen>
#endif

#include <string>
#include <utility>

#ifdef RADIO_SUITE_HAVE_LOGBOOK
#include "app/wsjtx_client.h"
#include "app/wsjtx_logbook.h"
#include "logbook/hamlog_workbench.h"
#endif

void MainWindow::openConnectDialog() {
  ConnectDialog dlg(radio::app::loadLinkPrefs(), this);
  if (dlg.exec() != QDialog::Accepted) return;
  radio::app::saveLinkPrefs(dlg.prefs());
  // 本次会话的 transport 已经建好，不能假装切了链路：文案只说已保存
  flashStatus(
      QString("链路设置已保存（%1），重启 radio-suite 生效")
          .arg(radio::app::linkName(dlg.prefs().link)),
      false);
}

// 桌面：一屏两列（C1/C2 量过，手机宽度放不下这套，手机上走 buildAndroidUi）
void MainWindow::buildDesktopUi() {
  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);

  // ---- 顶栏（主）：LCD 风频率带（原厂琥珀屏）+ PTT + 连接 ----
  auto* top = new QHBoxLayout();
  top->addWidget(buildLcdBand(central, false), 1);

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

  // 连接设置（原厂 Connect Setting 件）：保存写持久化偏好，重启后由 launch_config 建链路
  auto* conn_btn = new QPushButton("连接…", central);
  conn_btn->setMinimumSize(88, 40);
  connect(conn_btn, &QPushButton::clicked, this, &MainWindow::openConnectDialog);
  top->addWidget(conn_btn);
  root->addLayout(top, 0);

  // ---- 主体：左主列（仪表/调谐/功能/瀑布/日志） + 右竖大按键列 ----
  auto* body = new QHBoxLayout();
  auto* main_col = new QVBoxLayout();

  // 仪表 + 调谐同排：S 表靠左、旋钮居中、Po/SWR 缩到右侧
  auto* console_row = new QHBoxLayout();
  console_row->setSpacing(10);
  createMeters(central);
  console_row->addWidget(smeter_, 0, Qt::AlignVCenter);
  console_row->addWidget(buildDeckWidget(central), 1);
  console_row->addWidget(poswr_, 0, Qt::AlignVCenter);
  main_col->addLayout(console_row);
  main_col->addLayout(buildBandsRow(central));
  main_col->addLayout(buildVfoRow(central));

  // 制式/中继一行 + 功能开关一行
  auto* func_block = new QVBoxLayout();
  func_block->setSpacing(8);
  func_block->addLayout(buildModeRow(central));
  {
    auto* funcs = new QHBoxLayout();
    funcs->setSpacing(6);
    funcs->addLayout(buildTogglesRow(central));
    funcs->addSpacing(12);
    funcs->addLayout(buildRitRow(central));
    funcs->addSpacing(12);
    funcs->addLayout(buildMemRow(central));
    funcs->addSpacing(12);
    funcs->addLayout(buildAudioRow(central));
    funcs->addStretch(1);
    func_block->addLayout(funcs);
  }
  main_col->addLayout(func_block);

  waterfall_ = new WaterfallWidget(central);
  waterfall_->setDbRange(-110.0f, -10.0f);
  waterfall_->setMinimumHeight(260);
  waterfall_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  main_col->addWidget(waterfall_, 1);

  // 这条轴一直没有刻度：屏幕上是一片没有单位的彩色，谁都读不出信号在哪。
  // 先把轴说清楚 + 给一个跟随光标的读数（GL 控件里放不住文字，挂它下面一行）。
  // 注意这不是 RF 频谱——本仓没有任何 RF 跨度数据源，所以这里不做「点哪儿就调哪儿」。
  wf_axis_label_ = new QLabel(central);
  wf_axis_label_->setObjectName(QStringLiteral("wf_axis_label"));
  wf_axis_label_->setStyleSheet("color: #8a9bb4; font-size: 11px;");
  wf_axis_label_->setText(
      QStringLiteral("轴：RX 音频带 0–%1 kHz（非 RF 频谱）· 光标 —")
          .arg(static_cast<int>(kSampleRateHz / 2000.0)));
  main_col->addWidget(wf_axis_label_);
  waterfall_->installEventFilter(this);
  waterfall_->setMouseTracking(true);  // 没有按下也要收到 MouseMove

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
      // 带 parent 的 QWidget 默认是子控件：不加 Qt::Window 会内嵌在主窗口左上角
      // 盖住频谱/PTT，且没有标题栏、无法移动。
      wb->setWindowFlag(Qt::Window);
      wb->setWindowTitle("HamLog 工作台 — radio-suite");
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

  for (auto* b : buildBigButtons(central)) side->addWidget(b);
#ifdef RADIO_SUITE_HAVE_LOGBOOK
  if (hamlog_.available()) {
    createQsoWidgets(central, [side](QWidget* w) { side->addWidget(w); });
    wireHamlogPanels();
  }
#endif
  side->addStretch(1);
  body->addLayout(side);
  root->addLayout(body, 1);

  status_label_ = new QLabel(radio::app::linkStatusText(link_), central);
  root->addWidget(status_label_);

  setCentralWidget(central);
  setMinimumSize(1180, 860);
  setWindowTitle("radio-suite — IC-705 控制台");
}

void MainWindow::createPanels() {
  // APRS 面板先建（hamlog 块要用它 setLoginCall）
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

  // FT8 本机解码台：同一份 RX PCM 自己解，不依赖 WSJT-X 进程
  ft8_panel_ = new radio::app::Ft8Panel(nullptr);
  ft8_panel_->setWindowFlag(Qt::Window);
  ft8_panel_->setWindowTitle("FT8 本机解码 — radio-suite");
  ft8_panel_->resize(900, 560);
  ft8_panel_->hide();

  // 手机没有系统窗口标题栏/关闭按钮：面板各插一条「标题 + 关闭 ✕」，
  // 尺寸铺满可用屏幕；隐藏（关闭或系统返回键）时把大按键复位，
  // 否则再点一下只是把"看不见的已开窗"关掉，表现为切不过去
  const std::pair<QWidget*, const char*> kPanelTitles[] = {
      {aprs_panel_, "APRS 工作台"},
      {wsjtx_panel_, "WSJT-X 接入"},
      {ft8_panel_, "FT8 本机解码"},
  };
  for (const auto& [panel, title] : kPanelTitles) {
    radio::app::touch::addCloseBar(panel, title);
#ifdef Q_OS_ANDROID
    if (const QScreen* screen = QGuiApplication::primaryScreen())
      panel->resize(screen->availableGeometry().size());
#endif
    panel->installEventFilter(this);
  }
  connect(ft8_panel_, &radio::app::Ft8Panel::statusMessage, this,
          [this](const QString& m, bool err) { flashStatus(m, err); });
  connect(ft8_panel_, &radio::app::Ft8Panel::decodeActivated, this,
          [this](const QString& call, const QString& grid, quint64 hz,
                 const QString&) {
            if (qso_call_edit_) qso_call_edit_->setText(call);
            QString text = QString("FT8 解码：%1").arg(call);
            if (hz != 0)
              text += QString(" · %1 MHz").arg(hz / 1e6, 0, 'f', 6);
            if (!grid.isEmpty()) text += " · " + grid;
            flashStatus(text, false);
          });

  // FT8 发射出口：整槽 PCM 走本机声卡直出（与 SSTV 一键发射同一路），
  // PTT 由门禁键控、播完回 RX。不注进 50003 上行：桥队列 200ms 满则丢最旧，
  // 实时语音丢一帧无所谓，15 s 整槽丢不得。
  ft8_panel_->setTxController([this](std::vector<int16_t> pcm, uint32_t rate) {
    if (!ft8_tx_audio_) ft8_tx_audio_ = std::make_unique<radio::audio::AudioOut>();
    // 优先用「音频设置」里选的那块卡（-1 = 系统默认）
    int dev = network_audio_ ? network_audio_->outputDevice() : -1;
    if (dev < 0) dev = ft8_tx_audio_->defaultOutputDevice();
    if (dev < 0) return false;
    // 整槽发射必须独占：已经在 Tx（SSTV/语音正在播）时再叠一路，两路声卡流会
    // 一起进电台，解出来全是垃圾。beginScriptedTx 对已键控状态是放行的，所以自己拦。
    if (session_.gate().phase() == radio::core::PttPhase::Tx) return false;
    if (!session_.beginScriptedTx()) return false;
    const bool started = ft8_tx_audio_->playAsync(
        std::move(pcm), rate, static_cast<uint32_t>(dev),
        [this](bool ok, std::string err) {
          QMetaObject::invokeMethod(
              this,
              [this, ok, err] {
                session_.endScriptedTx();
                flashStatus(ok ? "FT8 整槽播完，已回 RX"
                               : QString("FT8 发射中断：%1").arg(err.c_str()),
                            !ok);
              },
              Qt::QueuedConnection);
        });
    if (!started) {
      session_.endScriptedTx();
      return false;
    }
    return true;
  });

  // FT8 音源「本机麦克风」：采集帧直灌面板，不接电台也能听空口
  // （把喇叭对着麦克风就能自解自己播的槽）。输入设备沿用「音频设置」的
  // audio/in_dev；网络音频在跑时它的 mic_ 已经占着输入设备，这里必须让路。
  ft8_panel_->setCaptureController(
      [this] {
        if (network_audio_ && network_audio_->active()) {
          flashStatus("FT8 麦克风音源让位：网络音频正占着麦克风上行，"
                      "先关网络音频或改回 50003 音源",
                      true);
          return false;
        }
        if (!ft8_capture_)
          ft8_capture_ = std::make_unique<radio::audio::AudioIn>();
        auto st = radio::ui::appSettings();
        int dev = st.value("audio/in_dev", -1).toInt();
        if (dev < 0) dev = ft8_capture_->defaultInputDevice();
        if (dev < 0) {
          flashStatus("FT8 麦克风音源：没有可用输入设备", true);
          return false;
        }
        std::string err;
        const bool ok = ft8_capture_->start(
            static_cast<uint32_t>(dev),
            [this](std::span<const int16_t> frame) {
              if (ft8_panel_) ft8_panel_->feedCapturePcm(frame);
            },
            &err);
        if (!ok) {
          flashStatus(QString("FT8 麦克风采集失败：%1").arg(err.c_str()), true);
          return false;
        }
        flashStatus("FT8 音源切到麦克风（48 kHz s16 mono 采集）", false);
        return true;
      },
      [this] {
        if (ft8_capture_) ft8_capture_->stop();
      });

  // 音源选择记住（-1/未设 = 50003 下行）：麦克风监听是「整晚挂着听」的用法
  {
    auto st = radio::ui::appSettings();
    ft8_panel_->setSource(
        st.value("ft8/source", 0).toInt() == 1
            ? radio::app::Ft8Panel::Source::Microphone
            : radio::app::Ft8Panel::Source::Network);
  }
  connect(ft8_panel_, &radio::app::Ft8Panel::sourceChanged, this,
          [](radio::app::Ft8Panel::Source source) {
            auto st = radio::ui::appSettings();
            st.setValue("ft8/source",
                        source == radio::app::Ft8Panel::Source::Microphone ? 1 : 0);
          });
}

void MainWindow::createMeters(QWidget* parent) {
  smeter_ = new SMeterWidget(parent);
  poswr_ = new PoSwrWidget(parent);
}

QWidget* MainWindow::buildLcdBand(QWidget* parent, bool compact) {
  // compact = 手机顶栏：字号/内边距收一档，376dp 高的横屏才有频谱的地儿
  auto* lcd = new QFrame(parent);
  lcd->setObjectName("lcdBand");
  lcd->setStyleSheet(
      "QFrame#lcdBand { background: #060804; border: 2px solid #2c313a;"
      " border-radius: 4px; }");
  auto* lcd_lay = new QHBoxLayout(lcd);
  lcd_lay->setContentsMargins(compact ? 6 : 16, compact ? 2 : 6,
                              compact ? 6 : 16, compact ? 2 : 6);
  if (compact) lcd_lay->setSpacing(6);
  freq_label_ = new QLabel("__.__._ MHz", lcd);
  QFont mono("Cascadia Mono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPointSize(compact ? 24 : 44);
  mono.setBold(true);
  freq_label_->setFont(mono);
  freq_label_->setStyleSheet("color: #ffb300; background: transparent;"
                             " qproperty-alignment: AlignCenter;");
  lcd_lay->addWidget(freq_label_, 1);
  mode_label_ = new QLabel("--", lcd);
  QFont moded = mode_label_->font();
  moded.setPointSize(compact ? 12 : 17);
  moded.setBold(true);
  mode_label_->setFont(moded);
  mode_label_->setStyleSheet("color: #7ee0a0; background: transparent;");
  mode_label_->setAlignment(Qt::AlignCenter);
  mode_label_->setMinimumWidth(compact ? 64 : 104);
  lcd_lay->addWidget(mode_label_);
  // 中继双频副显示：DUP± 时显示真实 TX 频率（offset 0x0C 读回，不猜惯例值）
  tx_label_ = new QLabel("", lcd);
  QFont txf = tx_label_->font();
  txf.setPointSize(compact ? 10 : 18);
  txf.setBold(true);
  tx_label_->setFont(txf);
  tx_label_->setStyleSheet("color:#ff8a65; background: transparent;");
  tx_label_->setAlignment(Qt::AlignCenter);
  tx_label_->setMinimumWidth(compact ? 0 : 250);
  lcd_lay->addWidget(tx_label_);
  return lcd;
}

QPushButton* MainWindow::makeBigButton(QWidget* parent, const QString& text,
                                       const QColor& c) {
  auto* b = new QPushButton(text, parent);
  b->setMinimumHeight(64);
  b->setMinimumWidth(130);
  QFont f = b->font();
  f.setPointSize(12);
  f.setBold(true);
  b->setFont(f);
  b->setStyleSheet(QString("QPushButton { background: %1; color: white; }"
                           "QPushButton:checked { background: #ffb300;"
                           " color: #1a1a1a; font-weight: bold;"
                           " border: 2px solid #ffd54f; }")
                       .arg(c.name()));
  return b;
}

std::vector<QPushButton*> MainWindow::buildBigButtons(QWidget* parent) {
  aprs_big_btn_ = makeBigButton(parent, "APRS\n台流", QColor(0x00, 0x69, 0x7c));
  aprs_big_btn_->setToolTip("APRS 工作台：地图/台站/消息/过滤器/RF+iGate");
  wsjtx_big_btn_ = makeBigButton(parent, "WSJT-X\n接入", QColor(0x1a, 0x23, 0x7e));
  wsjtx_big_btn_->setToolTip("WSJT-X UDP：解码/QSO/WSPR + Reply/Halt/FreeText");
  ft8_big_btn_ = makeBigButton(parent, "FT8\n本机", QColor(0x10, 0x51, 0x6b));
  ft8_big_btn_->setToolTip("FT8 本机解码：50003 下行音频直接解，不需要 WSJT-X");
  sstv_big_btn_ = makeBigButton(parent, "SSTV\n发送", QColor(0x8e, 0x24, 0xaa));
  sstv_big_btn_->setToolTip("慢扫描电视：图片→Robot36 音频（发送/接收解码）");
  dtmf_big_btn_ = makeBigButton(parent, "DTMF\n拨号", QColor(0x00, 0x69, 0x5c));
  dtmf_big_btn_->setToolTip("双音多频小键盘（ITU Q.23，生成 .wav）");
  auto* voice_big_btn_ = makeBigButton(parent, "VOICE\n录音", QColor(0x4a, 0x14, 0x8c));
  voice_big_btn_->setToolTip("Voice Memory：录音/列表/播放/门禁编排发射（原厂件）");
  auto* cw_big_btn_ = makeBigButton(parent, "CW\n键控", QColor(0x5d, 0x40, 0x37));
  cw_big_btn_->setToolTip("CW Keyer：0x17 文本发送 + 键速/侧音/半插入（原厂件）");
  auto* set_big_btn_ = makeBigButton(parent, "SET\n设置", QColor(0x37, 0x47, 0x4f));
  set_big_btn_->setToolTip("Set Mode 深设置树：0x1A 0x05 菜单项读写（原厂件）");
  auto* memch_big_btn_ = makeBigButton(parent, "MEM\n信道", QColor(0x00, 0x69, 0x5c));
  memch_big_btn_->setToolTip("记忆信道：0x1A 0x00 内容读写 + 0x08 组/信道选择（原厂件）");
  auto* check_big_btn_ = makeBigButton(parent, "自检\n台账", QColor(0x33, 0x2a, 0x1f));
  check_big_btn_->setToolTip(
      "链路与权限台账：这台设备上每条能力是真能用、被降级还是压根没编进来");
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
  connect(check_big_btn_, &QPushButton::clicked, this, [this] {
    radio::app::SelfCheckDialog dlg([this] { return selfCheckInput(); }, this);
    dlg.exec();
  });

  // 面板大按键切换显隐（独立顶层窗）
  // 三个面板互斥：手机上一屏放不下两扇独立窗，开一个就收掉另外两个
  const auto close_others = [this](QPushButton* keep) {
    for (auto* b : {aprs_big_btn_, wsjtx_big_btn_, ft8_big_btn_})
      if (b && b != keep && b->isChecked()) b->setChecked(false);
  };
  aprs_big_btn_->setCheckable(true);
  connect(aprs_big_btn_, &QPushButton::toggled, this, [this, close_others](bool on) {
    if (on) close_others(aprs_big_btn_);
    aprs_panel_->setVisible(on);
    if (on) {
      aprs_panel_->raise();
      aprs_panel_->activateWindow();
      if (!aprs_panel_->isConnected()) aprs_panel_->toggleConnect();
    }
  });

  wsjtx_big_btn_->setCheckable(true);
  connect(wsjtx_big_btn_, &QPushButton::toggled, this, [this, close_others](bool on) {
    if (on) close_others(wsjtx_big_btn_);
    wsjtx_panel_->setVisible(on);
    if (on) {
      wsjtx_panel_->raise();
      wsjtx_panel_->activateWindow();
      if (!wsjtx_panel_->isListening()) wsjtx_panel_->startListening();
    }
  });

  ft8_big_btn_->setCheckable(true);
  connect(ft8_big_btn_, &QPushButton::toggled, this, [this, close_others](bool on) {
    if (on) close_others(ft8_big_btn_);
    ft8_panel_->setVisible(on);
    if (on) {
      ft8_panel_->raise();
      ft8_panel_->activateWindow();
      if (!ft8_panel_->decoding()) ft8_panel_->startDecoding();
    } else {
      ft8_panel_->stopDecoding();
    }
  });
  return {aprs_big_btn_, wsjtx_big_btn_, ft8_big_btn_, sstv_big_btn_,
          dtmf_big_btn_, voice_big_btn_, cw_big_btn_, set_big_btn_,
          memch_big_btn_, check_big_btn_};
}

#ifdef RADIO_SUITE_HAVE_LOGBOOK
void MainWindow::createQsoWidgets(QWidget* parent,
                                  const std::function<void(QWidget*)>& place) {
  qso_call_edit_ = new QLineEdit(parent);
  qso_call_edit_->setPlaceholderText("对方呼号");
  place(qso_call_edit_);
  auto* log_btn = makeBigButton(parent, "记 QSO", QColor(0x2e, 0x7d, 0x32));
  log_btn->setMinimumHeight(48);
  place(log_btn);
  connect(log_btn, &QPushButton::clicked, this, &MainWindow::onLogQso);
  auto* adif_btn = makeBigButton(parent, "ADIF 导出", QColor(0x37, 0x47, 0x4f));
  adif_btn->setMinimumHeight(40);
  place(adif_btn);
  connect(adif_btn, &QPushButton::clicked, this, &MainWindow::onExportAdif);
}

QListWidget* MainWindow::ensureQsoList(QWidget* parent) {
  if (qso_list_) return qso_list_;
  qso_list_ = new QListWidget(parent);
  refreshQsoList();
  return qso_list_;
}

void MainWindow::wireHamlogPanels() {
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
