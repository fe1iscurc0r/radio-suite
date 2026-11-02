#include "app/MainWindow.h"
// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/MainWindow_android.cpp — MainWindow Android UI 构建域（同一类的第三实现文件）：
// 手机分页布局 + 桌面/手机共用的调谐台/频段/双 VFO/制式/开关/记忆/音频行控件构造。

#include "app/dial_widget.h"
#include "app/network_audio.h"
#include "app/repeater_dialog.h"
#include "app/touch_dialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScroller>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

namespace {

struct BandButton {
  const char* name;
  uint64_t center_hz;
  uint64_t low_hz;   // 高亮判定区间 [low, high)
  uint64_t high_hz;
};

// 常用频段快捷（中心频率取 CW/SSB 常用段）
constexpr BandButton kBands[] = {
    {"40m", 7'100'000, 7'000'000, 7'300'000},
    {"20m", 14'100'000, 14'000'000, 14'350'000},
    {"15m", 21'200'000, 21'000'000, 21'450'000},
    {"10m", 28'500'000, 28'000'000, 29'700'000},
    {"6m", 50'150'000, 50'000'000, 54'000'000},
    {"2m", 145'500'000, 144'000'000, 148'000'000},
    {"70cm", 433'000'000, 430'000'000, 440'000'000},
};

// 步进档（Hz）：互斥按钮组，默认 1 kHz
constexpr qint64 kSteps[] = {100, 1'000, 10'000, 100'000};
const char* kStepNames[] = {"100Hz", "1k", "10k", "100k"};
constexpr int kDefaultStep = 1;

QString stepCaption(qint64 hz) {
  return hz >= 1'000 ? QString("%1 kHz").arg(hz / 1'000)
                     : QString("%1 Hz").arg(hz);
}

}  // namespace

#ifdef Q_OS_ANDROID
void MainWindow::buildAndroidUi() {
  // 手机：内容区 843×376dp（锁横屏后实测），原两列布局必出界。
  // 这里只保留一条常驻顶栏 + 五个整页，页与页之间不共享纵向空间。
  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);
  root->setContentsMargins(4, 4, 4, 4);
  root->setSpacing(4);

  // ---- 常驻顶栏：LCD 频率/制式 + 状态 + PTT + 连接 ----
  // 状态行原先独占一行，在 376dp 高的屏幕上白吃一条频谱高度（用户原话）；
  // 现在并进顶栏。窄条放不下长句时裁掉尾部，完整文案仍在 flashStatus 的源头。
  auto* top = new QHBoxLayout();
  top->setSpacing(6);
  top->addWidget(buildLcdBand(central, true), 1);

  status_label_ = new QLabel(radio::app::linkStatusText(link_), central);
  status_label_->setStyleSheet("color: #aeb6c0; font-size: 8pt;");
  status_label_->setMinimumWidth(96);
  status_label_->setMaximumWidth(220);
  top->addWidget(status_label_);

  ptt_btn_ = new QPushButton("PTT", central);
  ptt_btn_->setMinimumSize(92, 56);
  ptt_btn_->setMaximumWidth(120);
  QFont bold = ptt_btn_->font();
  bold.setBold(true);
  bold.setPointSize(11);
  ptt_btn_->setFont(bold);
  connect(ptt_btn_, &QPushButton::clicked, this,
          [this] { session_.pttButtonPressed(); });
  top->addWidget(ptt_btn_);

  auto* conn_btn = new QPushButton("连接…", central);
  conn_btn->setMinimumSize(72, 48);
  conn_btn->setMaximumWidth(88);
  connect(conn_btn, &QPushButton::clicked, this, &MainWindow::openConnectDialog);
  top->addWidget(conn_btn);
  root->addLayout(top);

  pages_ = new QStackedWidget(central);
  root->addWidget(pages_, 1);

  // 比一屏高的页面套滚动：宁可滚，也不许再出现「按键被裁掉」（C1 原始症状）
  auto wrapScroll = [this](QWidget* page) -> QWidget* {
    auto* scroll = new QScrollArea(pages_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    // 手指拖动滚动（QScrollArea 默认只认滚轮/滚动条）
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
    return scroll;
  };

  // 1) 频谱：S 表 / Po-SWR 压成一条，其余高度全给瀑布
  {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    auto* meters = new QHBoxLayout();
    meters->setSpacing(8);
    createMeters(page);
    meters->addWidget(smeter_, 0, Qt::AlignVCenter);
    meters->addStretch(1);
    meters->addWidget(poswr_, 0, Qt::AlignVCenter);
    lay->addLayout(meters);
    waterfall_ = new WaterfallWidget(page);
    waterfall_->setDbRange(-110.0f, -10.0f);
    waterfall_->setMinimumHeight(120);
    waterfall_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lay->addWidget(waterfall_, 1);
    pages_->addWidget(page);
  }

  // 2) 调谐：旋钮台 + 频段（点频段就在本页调谐）+ 双 VFO
  {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(buildDeckWidget(page));
    lay->addLayout(buildBandsRow(page));
    lay->addLayout(buildVfoRow(page));
    lay->addStretch(1);
    pages_->addWidget(wrapScroll(page));
  }

  // 3) 制式：制式组 + 中继 + 功能开关 + 记忆 + 音频
  {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);
    lay->addLayout(buildModeRow(page));
    lay->addLayout(buildTogglesRow(page));
    lay->addLayout(buildRitRow(page));
    lay->addLayout(buildMemRow(page));
    lay->addLayout(buildAudioRow(page));
    lay->addStretch(1);
    pages_->addWidget(wrapScroll(page));
  }

  // 4) 面板：独立工作台入口，3 列网格（比桌面竖列矮一半）
  {
    auto* page = new QWidget(pages_);
    auto* grid = new QGridLayout(page);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(6);
    const auto big = buildBigButtons(page);
    for (size_t i = 0; i < big.size(); ++i)
      grid->addWidget(big[i], static_cast<int>(i / 3),
                      static_cast<int>(i % 3));
    // 拉伸行必须落在最后一个按钮的下一行：10 个键占满 0..3 行，
    // 再钉在 3 行会把最后一个键拉成整屏高。
    grid->setRowStretch((static_cast<int>(big.size()) + 2) / 3, 1);
    pages_->addWidget(page);
  }

  // 5) 日志：HamLog 最近 QSO + 记 QSO / ADIF
  //    库不在就整页不出现：宁可只有四个页签，也不摆一个永远打不开的死页面。
  //    Android 上构造里已 ensureLocalDb() 自建本地库，所以这一页是第五个页签。
  bool have_log_page = false;
#ifdef RADIO_SUITE_HAVE_LOGBOOK
  if (hamlog_.available()) {
    have_log_page = true;
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(ensureQsoList(page), 1);
    auto* entry = new QHBoxLayout();
    entry->setSpacing(6);
    createQsoWidgets(page, [entry](QWidget* w) { entry->addWidget(w); });
    lay->addLayout(entry);
    wireHamlogPanels();
    pages_->addWidget(page);
  }
#endif

  // ---- 底部页签 ----
  auto* tab_row = new QHBoxLayout();
  tab_row->setSpacing(4);
  auto* group = new QButtonGroup(central);
  group->setExclusive(true);
  QStringList tabs{"频谱", "调谐", "制式", "面板"};
  if (have_log_page) tabs << "日志";
  for (int i = 0; i < tabs.size(); ++i) {
    auto* b = new QPushButton(tabs[i], central);
    b->setCheckable(true);
    b->setChecked(i == 0);
    b->setMinimumHeight(44);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(b, &QPushButton::clicked, this,
            [this, i] { pages_->setCurrentIndex(i); });
    group->addButton(b, i);
    tab_row->addWidget(b, 1);
  }
  root->addLayout(tab_row);

  // 触摸目标：桌面那套 28-34dp 是鼠标尺寸，手机上统一抬到 44dp
  for (auto* b : central->findChildren<QPushButton*>()) {
    if (b->minimumHeight() < 44) b->setMinimumHeight(44);
  }

  setCentralWidget(central);
  setWindowTitle("radio-suite — IC-705 控制台");
}
#endif

QWidget* MainWindow::buildDeckWidget(QWidget* parent) {
  // ---- 紧凑旋钮台：旋钮 + 步进档 + 升降频/手动输频 ----
  auto* deck = new QFrame(parent);
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
  step_label_->setText(
      QString("步进 %1（旋钮 / ▼▲ 每格）").arg(stepCaption(kSteps[kDefaultStep])));
  return deck;
}

QHBoxLayout* MainWindow::buildBandsRow(QWidget* parent) {
  // ---- 频段快捷 + 接收波段（可听不可发）----
  auto* bands = new QHBoxLayout();
  bands->setSpacing(5);
  for (const auto& b : kBands) {
    auto* btn = new QPushButton(b.name, parent);
    btn->setMinimumSize(54, 28);
    btn->setCheckable(true);  // 高亮由 refreshState 按当前频率回读，不当互斥组用
    btn->setProperty("center_hz", static_cast<qulonglong>(b.center_hz));
    connect(btn, &QPushButton::clicked, this, &MainWindow::onBandClicked);
    bands->addWidget(btn);
    band_btns_.push_back(btn);
    band_low_.push_back(b.low_hz);
    band_high_.push_back(b.high_hz);
  }
  bands->addStretch(1);
  auto mk_preset = [&](const QString& text, const QString& tip, uint64_t low,
                       uint64_t high, auto action) {
    auto* btn = new QPushButton(text, parent);
    btn->setToolTip(tip);
    btn->setMinimumSize(64, 28);
    btn->setCheckable(true);
    connect(btn, &QPushButton::clicked, this, action);
    bands->addWidget(btn);
    band_btns_.push_back(btn);
    band_low_.push_back(low);
    band_high_.push_back(high);
  };
  mk_preset("AIR", "民航话音 118-137（AM，中点 127.5，可听不可发）",
            118'000'000, 137'000'000, [this] {
              session_.setMode(radio::core::civ::Mode::AM);
              session_.tuneTo(127'500'000);
            });
  mk_preset("BC-FM", "调频广播 87.5-108（WFM，中点 97.75，可听不可发）",
            87'500'000, 108'000'000, [this] {
              session_.setMode(radio::core::civ::Mode::WFM);
              session_.tuneTo(97'750'000);
            });
  mk_preset("BC-SW", "短波广播 31m（AM，9.5，可听不可发）", 9'400'000,
            9'900'000, [this] {
              session_.setMode(radio::core::civ::Mode::AM);
              session_.tuneTo(9'500'000);
            });
  return bands;
}

QHBoxLayout* MainWindow::buildVfoRow(QWidget* parent) {
  // ---- 双 VFO（SUB 带）----
  auto* vfo = new QHBoxLayout();
  vfo->setSpacing(5);
  vfo->addWidget(new QLabel("双 VFO", parent));
  sub_label_ = new QLabel("—", parent);
  sub_label_->setMinimumWidth(190);
  QFont subf("Cascadia Mono");
  subf.setStyleHint(QFont::Monospace);
  subf.setPointSize(11);
  sub_label_->setFont(subf);
  sub_label_->setStyleSheet("color: #4dd0e1;");
  vfo->addWidget(sub_label_);

  auto mk_vfo = [&](const QString& text, const QString& tip) {
    auto* b = new QPushButton(text, parent);
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
  auto* dual_info = new QPushButton("双守听？", parent);
  dual_info->setToolTip("IC-705 无 DUAL 双守听：单接收机。用 A/B 双 VFO 手动守听 SUB 带。");
  dual_info->setMinimumSize(84, 28);
  dual_info->setStyleSheet(
      "QPushButton { border-color: #546e7a; color: #90caf9; }");
  connect(dual_info, &QPushButton::clicked, this, [this] {
    radio::app::touch::showInfo(
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
  return vfo;
}

QHBoxLayout* MainWindow::buildModeRow(QWidget* parent) {
  // 行 1：模式组（回读驱动）+ 中继频差方向 + 中继/亚音对话框
  auto* modes = new QHBoxLayout();
  modes->setSpacing(6);
  const std::pair<const char*, radio::core::civ::Mode> kModes[] = {
      {"LSB", radio::core::civ::Mode::LSB}, {"USB", radio::core::civ::Mode::USB},
      {"AM", radio::core::civ::Mode::AM},   {"CW", radio::core::civ::Mode::CW},
      {"FM", radio::core::civ::Mode::FM},   {"WFM", radio::core::civ::Mode::WFM}};
  for (const auto& [name, m] : kModes) {
    auto* btn = new QPushButton(name, parent);
    btn->setCheckable(true);
    // 独占：制式互斥这件事不能只靠回读。点了就 setChecked(true)，而唯一的
    // 取消路径在 syncTogglesFromState()——链路没通（电台关机 / UDP 对端不应答）
    // 就一帧回读都没有，连点几个就同时亮几个（2026-10-31 真机）。
    btn->setAutoExclusive(true);
    btn->setMinimumSize(56, 34);
    const auto mode = m;
    connect(btn, &QPushButton::clicked, this, [this, btn, mode] {
      session_.setMode(mode);
      btn->setChecked(true);  // 乐观更新，250ms 回读纠偏
    });
    mode_btns_.push_back(btn);
    mode_vals_.push_back(m);
    modes->addWidget(btn);
  }
  modes->addSpacing(12);
  dup_btn_ = new QPushButton("SPLX", parent);
  dup_btn_->setToolTip("中继频差方向：SPLX → DUP− → DUP+（0x0F）");
  dup_btn_->setMinimumSize(72, 34);
  connect(dup_btn_, &QPushButton::clicked, this, [this] {
    const int cur = session_.store().snapshot().duplex_raw;
    const uint8_t next = cur == 0x00 || cur == 0x10 ? 0x11 : (cur == 0x11 ? 0x12 : 0x10);
    session_.setDuplex(next);
  });
  modes->addWidget(dup_btn_);
  repeater_btn_ = new QPushButton("中继/亚音…", parent);
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
  return modes;
}

QHBoxLayout* MainWindow::buildTogglesRow(QWidget* parent) {
  // 行 2：功能开关（回读驱动）
  auto* row = new QHBoxLayout();
  row->setSpacing(6);
  auto mk_toggle = [&](const QString& name, const QString& tip, auto setter) {
    auto* btn = new QPushButton(name, parent);
    btn->setCheckable(true);
    btn->setToolTip(tip);
    btn->setMinimumSize(56, 32);
    connect(btn, &QPushButton::clicked, this, [btn, setter] { setter(btn->isChecked()); });
    row->addWidget(btn);
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
  pamp_btn_ = new QPushButton("PAMP", parent);
  pamp_btn_->setToolTip("前置放大器循环：OFF → P1 → P2");
  pamp_btn_->setMinimumSize(88, 32);
  connect(pamp_btn_, &QPushButton::clicked, this, [this] {
    const int cur = session_.store().snapshot().pamp;
    session_.setSw16(radio::core::civ::kSub16Pamp,
                     static_cast<uint8_t>((cur + 1) % 3));
  });
  row->addWidget(pamp_btn_);
  agc_btn_ = new QPushButton("AGC", parent);
  agc_btn_->setToolTip("AGC 循环：F → M → S");
  agc_btn_->setMinimumSize(74, 32);
  connect(agc_btn_, &QPushButton::clicked, this, [this] {
    const int cur = session_.store().snapshot().agc;
    session_.setSw16(radio::core::civ::kSub16Agc,
                     static_cast<uint8_t>(cur >= 3 ? 1 : cur + 1));
  });
  row->addWidget(agc_btn_);
  return row;
}

QHBoxLayout* MainWindow::buildRitRow(QWidget* parent) {
  auto* row = new QHBoxLayout();
  row->setSpacing(6);
  auto mk_rit_toggle = [&](const QString& name, const QString& tip, auto setter) {
    auto* btn = new QPushButton(name, parent);
    btn->setCheckable(true);
    btn->setToolTip(tip);
    btn->setMinimumSize(58, 32);
    connect(btn, &QPushButton::clicked, this, [btn, setter] { setter(btn->isChecked()); });
    row->addWidget(btn);
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
  return row;
}

QHBoxLayout* MainWindow::buildMemRow(QWidget* parent) {
  // 本地记忆（电台内存信道 M3 接；本地槽即刻可用）
  auto* row = new QHBoxLayout();
  row->setSpacing(6);
  mem_slot_ = new QComboBox(parent);
  mem_slot_->setMinimumSize(76, 32);
  for (int i = 1; i <= 8; ++i) mem_slot_->addItem(QString("M%1").arg(i));
  row->addWidget(mem_slot_);
  auto* mem_save = new QPushButton("存", parent);
  mem_save->setToolTip("当前频率/模式存入所选槽（本地）");
  mem_save->setMinimumSize(48, 32);
  connect(mem_save, &QPushButton::clicked, this, &MainWindow::onMemSave);
  row->addWidget(mem_save);
  auto* mem_recall = new QPushButton("取", parent);
  mem_recall->setToolTip("取出所选槽的频率/模式");
  mem_recall->setMinimumSize(48, 32);
  connect(mem_recall, &QPushButton::clicked, this, &MainWindow::onMemRecall);
  row->addWidget(mem_recall);
  return row;
}

QHBoxLayout* MainWindow::buildAudioRow(QWidget* parent) {
  // 网络音频（RS-BA1 50003）：RX→扬声器、麦克风→PTT 上行
  auto* row = new QHBoxLayout();
  row->setSpacing(6);
  audio_btn_ = new QPushButton("网络音频", parent);
  audio_btn_->setCheckable(true);
  audio_btn_->setMinimumSize(96, 32);
  const bool has_audio = session_.audioLink() != nullptr;
  audio_btn_->setEnabled(has_audio);
  audio_btn_->setToolTip(
      has_audio
          ? "RS-BA1 50003：RX 下行→扬声器；麦克风→PTT 上行（设备在「音频设置」里选，48kHz s16le）"
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
  row->addWidget(audio_btn_);
  audio_setup_btn_ = new QPushButton("音频设置", parent);
  audio_setup_btn_->setToolTip("选择麦克风/扬声器，并查看 50003 下行丢包与设备欠载计数");
  audio_setup_btn_->setMinimumSize(84, 32);
  connect(audio_setup_btn_, &QPushButton::clicked, this,
          &MainWindow::onAudioSettings);
  row->addWidget(audio_setup_btn_);
  return row;
}
