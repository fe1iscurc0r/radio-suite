// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/MainWindow.h"

// radio-suite · app/MainWindow.cpp — 主窗口核心域：构造/关闭/主题/回读刷新/合成谱工作台。
// 同一类的其余实现按域拆分：桌面 UI 构建 → MainWindow_panels.cpp，
// Android UI 构建 + 共用控件构造 → MainWindow_android.cpp，
// 用户动作/持久化 → MainWindow_actions.cpp。类与 MainWindow.h 零改动。

#include "app/aprs_panel.h"
#include "app/ft8_panel.h"
#include "app/meter_widgets.h"
#include "app/network_audio.h"
#include "app/touch_dialog.h"
#include "app/wsjtx_panel.h"  // eventFilter 里比较面板指针需要完整类型

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QEvent>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QTimer>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QPermissions>  // Qt 6.7 没有独立的 QMicrophonePermission 头
#endif

#include <algorithm>
#include <utility>

#include "audio/audio_out.h"
#include "ui_common/app_settings.h"

namespace {

QString formatFreq(uint64_t hz) {
  return QString("%1.%2 MHz")
      .arg(hz / 1'000'000)
      .arg(hz % 1'000'000, 6, 10, QChar('0'));
}

}  // namespace

MainWindow::MainWindow(std::shared_ptr<radio::core::ICivTransport> transport,
                       radio::app::LinkPrefs link, QWidget* parent)
    : QMainWindow(parent), link_(std::move(link)), session_(std::move(transport)) {
  // 下拉弹窗的触摸坐标修复要在任何弹窗弹出前装好（桌面空操作）
  radio::app::touch::installPopupOriginFix();
#ifdef RADIO_SUITE_HAVE_LOGBOOK
#ifdef Q_OS_ANDROID
  // buildUi() 里靠 hamlog_.available() 决定「日志」页出不出现，而 available()
  // 只看文件在不在：Android 上没有 HamLog 桌面程序来建这个库，不自己建就永远只有四页。
  {
    QString err;
    if (!radio::logbook::HamLogDb::ensureLocalDb(&err))
      qWarning() << "radio-suite: 本地日志库建库失败" << err;
  }
#endif
#endif
  buildUi();

#ifdef Q_OS_ANDROID
  // AAudio 录音（50003 上行 / Voice Memory）要 RECORD_AUDIO 运行时授权：
  // 启动时申请一次，避免用户开网络音频时才因无权限静默失败
  {
    QMicrophonePermission mic_permission;
    if (qApp->checkPermission(mic_permission) ==
        Qt::PermissionStatus::Undetermined)
      qApp->requestPermission(mic_permission, [](const QPermission&) {});
  }
#endif

  samples_.assign(kFftSize, 0.0f);
  rx_ring_.assign(kFftSize, 0);
  col_db_.assign(static_cast<size_t>(WaterfallWidget::kTexW), -120.0f);
  row_bytes_.assign(static_cast<size_t>(WaterfallWidget::kTexW), 0);
  // 4 帧 Welch 功率平均：噪底抖动约减半，单音幅度不变（时间常数 ~0.26s）
  analyzer_.setAveragingFrames(4);

  // 50003 音频链：RX 播放 + PTT 门禁上行 + 真实频谱 tap
  network_audio_ = std::make_unique<radio::app::NetworkAudio>(&session_);
  {
    // 上次在「音频设置」里选的设备（-1 = 系统默认）
    auto st = radio::ui::appSettings();
    network_audio_->setDevices(st.value("audio/in_dev", -1).toInt(),
                               st.value("audio/out_dev", -1).toInt());
  }
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
    if (ft8_panel_) ft8_panel_->feedRxPcm(pcm);
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
  connect(&session_, &radio::app::RadioSession::linkChanged, this,
          [this](bool alive) {
            // G-6：断链要当场说破，不能继续显示上一次的值装作在线
            flashStatus(alive ? QStringLiteral("电台链路恢复：又开始回话了")
                              : QStringLiteral(
                                    "电台链路失联：超过 2 s 没有任何回帧"),
                        !alive);
          });
  connect(&session_, &radio::app::RadioSession::commandRejected, this,
          [this](quint8 opcode) {
            flashStatus(QStringLiteral("电台拒绝了命令 0x%1（频段限制或参数）")
                            .arg(opcode, 2, 16, QLatin1Char('0')),
                        true);
          });
  // 瀑布点击调频：本仓瀑布是解调音频单边谱（0..24 kHz），LSB/USB 下点击列
  // 换算成 RF 偏移、把点的信号拉回 1 kHz 音调处；AM/FM 解调音频与 RF 频谱
  // 没有单值映射，点击只闪提示。RF 域点击要等 sidecar IQ 频谱（见调研 §8）
  connect(waterfall_, &WaterfallWidget::freqPicked, this, [this](double frac) {
    const auto s = session_.store().snapshot();
    const double audio_hz = frac * 24000.0;
    const uint64_t off = static_cast<uint64_t>(
        audio_hz > 1'000.0 ? audio_hz - 1'000.0 : audio_hz);
    const QString mn = radio::core::civ::modeName(s.mode);  // QString：== 是内容比较（const char* == 字面量是指针比较，C4130）
    if (mn == "USB") {
      session_.tuneTo(s.freq_hz + off);
    } else if (mn == "LSB") {
      if (s.freq_hz > off) session_.tuneTo(s.freq_hz - off);
    } else {
      flashStatus("当前模式下瀑布是解调音频谱，点击调频仅 LSB/USB 有效", true);
    }
  });
  connect(&bench_timer_, &QTimer::timeout, this, &MainWindow::onBenchTick);
  // 33 ms ≈ 30 fps。不能更慢：FFT 窗只有 2048/48000 = 42.7 ms，原来 66 ms
  // 一拍等于每两拍之间有 23 ms 音频根本没被分析过（信号会整段漏掉），
  // 画面上也就是这种一格一格的段落感
  // Windows 默认定时器粒度 15.6 ms，33 ms 实测只跑到 21 fps（间隔 47 ms）；
  // PreciseTimer 才会去要 1 ms 粒度。安卓本来就是单调时钟精确唤醒
  bench_timer_.setTimerType(Qt::PreciseTimer);
  bench_timer_.start(33);

  // 显示层刷新：倒计时/确认窗口剩余秒走秒（判定与死限都在 core）
  connect(&ui_timer_, &QTimer::timeout, this, &MainWindow::refreshPtt);
  ui_timer_.start(250);

  connect(&status_timer_, &QTimer::timeout, this, [this] {
    status_label_->setStyleSheet("");
    status_label_->setText(radio::app::linkStatusText(link_));
  });
  status_timer_.setSingleShot(true);

  session_.start();
  refreshState();
  refreshPtt();
}

MainWindow::~MainWindow() {
  // 先停音频（会发状态信号，此时成员仍存活），再让成员逆序析构
  if (network_audio_) network_audio_->stop();
  // 面板是独立顶层窗口（父为空），析构前先停解码线程
  if (ft8_panel_) ft8_panel_->stopDecoding();
  // FT8 发射中途退窗：等播放线程收手，再强制回 RX——它的收尾回调排在事件队列里，
  // 关闭路上不一定还有机会跑，不能指望它把 PTT 放掉
  if (ft8_tx_audio_) {
    ft8_tx_audio_->wait();
    session_.endScriptedTx();
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  // APRS / WSJT-X / FT8 面板是无父顶层窗口：只要还有一个可见，Qt 就不退出，
  // 于是「关掉主窗」变成进程留在原地、FT8 解码线程继续跑。
  if (ft8_panel_) {
    ft8_panel_->cancelTransmit();  // 排期未到的整槽不许在关窗路上起播
    ft8_panel_->stopDecoding();
  }
  for (QWidget* w : QApplication::topLevelWidgets()) {
    if (w != this && w->isVisible()) w->hide();
  }
  QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  // 手机系统返回键 / 关闭 ✕ 都是直接 hide 顶层窗，按钮不跟着复位就会
  // 出现「第一下点了没反应、第二下才关」的幻觉
  if (event->type() == QEvent::Hide) {
    if (watched == aprs_panel_ && aprs_big_btn_)
      aprs_big_btn_->setChecked(false);
    else if (watched == wsjtx_panel_ && wsjtx_big_btn_)
      wsjtx_big_btn_->setChecked(false);
    else if (watched == ft8_panel_ && ft8_big_btn_)
      ft8_big_btn_->setChecked(false);
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
  // 键盘调谐（GQRX/SDR++ 同款交互）：事件从焦点控件沿父链上传，QLineEdit
  // 聚焦时输入/光标键被它先消费、按钮聚焦时空格被按钮消费，主窗只兜底
  switch (event->key()) {
    case Qt::Key_Left:  onTuneStep(-1); return;
    case Qt::Key_Right: onTuneStep(+1); return;
    case Qt::Key_PageDown: onTuneStep(-10); return;
    case Qt::Key_PageUp:   onTuneStep(+10); return;
    case Qt::Key_Space:
      // 与 PTT 按钮同一出口：两击门禁由会话编排（Rx→确认→Tx；Tx/确认态回 Rx）
      session_.pttButtonPressed();
      return;
    case Qt::Key_Escape:
      if (session_.gate().phase() != radio::core::PttPhase::Rx)
        session_.pttButtonPressed();  // 确认态取消 / 发射态回 RX
      return;
    default:
      QMainWindow::keyPressEvent(event);
  }
}

void MainWindow::buildUi() {
  applyTheme();
  createPanels();
#ifdef Q_OS_ANDROID
  // 手机锁横屏后内容区只有 843×376dp：原两列一屏布局必然出界（C1/C2/C6 真机
  // FAIL），这里走分页版；桌面仍走原布局。
  buildAndroidUi();
#else
  buildDesktopUi();
#endif
}

void MainWindow::applyTheme() {
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
}

void MainWindow::syncTogglesFromState(const radio::core::RadioState& s) {
  // 回读驱动：模式/功能按钮以电台回读为准（点击时的乐观更新在此纠偏，
  // 含 NAK 场景——如 ATT 在非 HF 段被电台拒绝，按钮会弹回）
  const auto block = [](QPushButton* b, bool checked) {
    QSignalBlocker guard(b);
    b->setChecked(checked);
  };
  // 按按钮自己的制式值比，不能拿数组下标当 Mode 枚举值：Mode 里有 RTTY=4，
  // 而按钮行没有 RTTY——按下标比会把 FM 的回读点亮成 WFM、WFM 的回读谁都不点。
  for (size_t i = 0; i < mode_btns_.size(); ++i)
    block(mode_btns_[i], s.mode == mode_vals_[i]);
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
  const auto rb = [](QPushButton* b, bool on) { QSignalBlocker g(b); b->setChecked(on); };
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
  if (ft8_panel_) ft8_panel_->setDialHz(s.freq_hz);
  syncBandHighlight(s.freq_hz);
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
#ifdef Q_OS_ANDROID
  // 手机顶栏只给双频（DUP±）留位置：simplex 时「TX = RX」不携带信息，
  // 常驻会白占一百多 dp，把频率大字挤到折行
  tx_label_->setVisible(s.duplex_raw == 0x11 || s.duplex_raw == 0x12);
#endif
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

void MainWindow::flashStatus(const QString& text, bool warn) {
  status_label_->setText(text);
  status_label_->setStyleSheet(
      warn ? "color: #ff5252; font-weight: bold;" : "color: #69f0ae;");
  status_timer_.start(3000);
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

  // 1025 bins → 1024 显示列：按列区间取最大（保尖峰）。
  // 注意 kFftSize=2048 只有 1025 个 bin——曾按 db[i*2] 取对，越界读 1KB，
  // 导致瀑布/谱线右半屏显示为常量色块。
  const auto& db = analyzer_.powerDb();
  const size_t bins = db.size();
  for (size_t i = 0; i < col_db_.size(); ++i) {
    const size_t b0 = i * bins / col_db_.size();
    const size_t b1 = std::min(bins, (i + 1) * bins / col_db_.size());
    float v = db[b0];
    for (size_t b = b0 + 1; b < b1; ++b) v = std::max(v, db[b]);
    col_db_[i] = v;
  }

  wf_proc_.process(col_db_.data(), col_db_.size(), row_bytes_.data());
  waterfall_->pushRow(row_bytes_.data(), static_cast<int>(row_bytes_.size()));
  waterfall_->pushSpectrum(col_db_.data(), static_cast<int>(col_db_.size()));
}
