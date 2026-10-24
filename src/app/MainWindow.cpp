// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/MainWindow.h"

#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

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
  root->addLayout(tune);

  // ---- 瀑布 ----
  waterfall_ = new WaterfallWidget(central);
  waterfall_->setDbRange(-110.0f, -10.0f);
  root->addWidget(waterfall_, 1);

  status_label_ = new QLabel("transport: mock · radio: IC-705 sim", central);
  root->addWidget(status_label_);

  setCentralWidget(central);
  setWindowTitle("radio-suite — M0 spike");
}

void MainWindow::refreshState() {
  const auto s = session_.store().snapshot();
  freq_label_->setText(formatFreq(s.freq_hz));
  mode_label_->setText(QString("%1/%2").arg(radio::core::civ::modeName(s.mode)).arg(s.filter));
  smeter_label_->setText(
      QString("S%1 (%2 dBm)")
          .arg(s.smeter_raw * 9.0 / 120.0, 0, 'f', 1)
          .arg(s.smeter_dbm, 0, 'f', 1));
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
  flashStatus(QString("已拒绝 %1 MHz：超出业余频段白名单").arg(hz / 1'000'000.0, 0, 'f', 3),
              true);
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
