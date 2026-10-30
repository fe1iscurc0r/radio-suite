// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/ft8_panel.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <memory>

#include "app/decode_target.h"

namespace radio::app {
namespace {

constexpr int kMaxRows = 500;  // 与 WSJT-X 面板解码表同量级

QString snrText(float snr) { return QString::number(snr, 'f', 0); }
QString dtText(float dt) { return QString::number(dt, 'f', 1); }
QString dfText(float df) { return QString::number(df, 'f', 0); }

}  // namespace

Ft8Panel::Ft8Panel(QWidget* parent) : QWidget(parent) { buildUi(); }

Ft8Panel::~Ft8Panel() = default;

void Ft8Panel::buildUi() {
  setWindowTitle("FT8 本机解码 — radio-suite");
  resize(900, 560);

  auto* root = new QVBoxLayout(this);
  auto* top = new QHBoxLayout();
  start_btn_ = new QPushButton("开始解码", this);
  clear_btn_ = new QPushButton("清空", this);
  status_label_ = new QLabel("未开始（需要先开「网络音频」拿到 50003 下行）", this);
  stats_label_ = new QLabel("", this);
  stats_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  top->addWidget(start_btn_);
  top->addWidget(clear_btn_);
  top->addWidget(status_label_, 1);
  top->addWidget(stats_label_);
  root->addLayout(top);

  progress_ = new QProgressBar(this);
  progress_->setRange(0, 100);
  progress_->setValue(0);
  progress_->setFormat("时隙 %p%");
  root->addWidget(progress_);

  table_ = new QTableWidget(0, 5, this);
  table_->setHorizontalHeaderLabels({"时间 UTC", "SNR", "DT", "DF", "文本"});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->verticalHeader()->setVisible(false);
  table_->setAlternatingRowColors(true);
  root->addWidget(table_, 1);

  auto* hint = new QLabel(
      "双击解码行：呼号/网格 → 主窗，频率 = 刻度 + DF。解码在本机完成，"
      "不需要 WSJT-X。",
      this);
  hint->setWordWrap(true);
  root->addWidget(hint);

  connect(start_btn_, &QPushButton::clicked, this, [this] {
    if (decoding()) stopDecoding(); else startDecoding();
  });
  connect(clear_btn_, &QPushButton::clicked, this, &Ft8Panel::clearRows);
  connect(table_, &QTableWidget::cellDoubleClicked, this,
          [this](int row, int) { activateDecode(row); });

  decoder_.setCallback([this](const radio::ft8::SlotResult& result) {
    // 解码线程 → GUI 线程；对象析构后 Qt 会丢弃未执行的投递
    auto copy = std::make_shared<radio::ft8::SlotResult>(result);
    QMetaObject::invokeMethod(
        this, [this, copy] { appendResult(*copy); }, Qt::QueuedConnection);
  });

  tick_ = new QTimer(this);
  connect(tick_, &QTimer::timeout, this, &Ft8Panel::refreshTick);
  tick_->start(250);
}

bool Ft8Panel::decoding() const { return decoder_.running(); }

void Ft8Panel::startDecoding(int64_t utc_ms) {
  const int64_t wait_ms = decoder_.start(
      utc_ms >= 0 ? utc_ms
                  : static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch()));
  start_btn_->setText("停止解码");
  emit statusMessage(
      wait_ms <= 0
          ? "FT8 本机解码已开始（当前时隙）"
          : QString("FT8 本机解码已开始，等待下一个 15 s 时隙（%1 s）")
                .arg(wait_ms / 1000.0, 0, 'f', 1),
      false);
  refreshTick();
}

void Ft8Panel::stopDecoding() {
  decoder_.stop();
  start_btn_->setText("开始解码");
  progress_->setValue(0);
  status_label_->setText("已停止");
  emit statusMessage("FT8 本机解码已停止", false);
}

void Ft8Panel::feedRxPcm(std::span<const int16_t> pcm) {
  if (!decoder_.running()) return;
  decoder_.push(pcm);
}

void Ft8Panel::refreshTick() {
  if (!decoder_.running()) {
    progress_->setValue(0);
    return;
  }
  progress_->setValue(
      static_cast<int>(std::lround(decoder_.slotProgress() * 100.0)));
  if (decoder_.decoding()) {
    status_label_->setText("解码中…");
  } else if (decoder_.fedSamples() == 0) {
    status_label_->setText("等待 50003 下行音频（网络音频未开或链路无采样）");
  } else {
    status_label_->setText(QString("采集中 · 已解 %1 槽%2")
                               .arg(decoder_.slotsDecoded())
                               .arg(decoder_.droppedSlots() > 0
                                        ? QString(" · 丢弃 %1 槽")
                                              .arg(decoder_.droppedSlots())
                                        : QString()));
  }
}

void Ft8Panel::clearRows() {
  table_->setRowCount(0);
  rows_.clear();
  stats_label_->clear();
}

void Ft8Panel::appendResult(const radio::ft8::SlotResult& result) {
  const QDateTime slot = QDateTime::fromMSecsSinceEpoch(result.utc_ms).toUTC();
  for (const auto& msg : result.messages) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    const QDateTime at =
        QDateTime::fromMSecsSinceEpoch(result.utc_ms + static_cast<int64_t>(
                                            std::llround(msg.dt_s * 1000.0)))
            .toUTC();
    table_->setItem(row, 0, new QTableWidgetItem(at.toString("hh:mm:ss")));
    table_->setItem(row, 1, new QTableWidgetItem(snrText(msg.snr_db)));
    table_->setItem(row, 2, new QTableWidgetItem(dtText(msg.dt_s)));
    table_->setItem(row, 3, new QTableWidgetItem(dfText(msg.df_hz)));
    table_->setItem(row, 4,
                    new QTableWidgetItem(QString::fromStdString(msg.text)));
    rows_.push_back({QString::fromStdString(msg.text), msg.df_hz});
  }
  stats_label_->setText(QString("%1 槽 · %2 条 · 解码 %3 ms")
                            .arg(slot.toString("hh:mm:ss"))
                            .arg(result.messages.size())
                            .arg(result.decode_ms, 0, 'f', 0));
  // 常开控制台：FT8 每 15 s 一槽、单槽可达几十条，不设上限几小时就上万行
  while (table_->rowCount() > kMaxRows) {
    table_->removeRow(0);
    rows_.erase(rows_.begin());
  }
}

int Ft8Panel::decodeRowCount() const { return table_->rowCount(); }

QString Ft8Panel::decodeText(int row) const {
  if (row < 0 || row >= table_->rowCount()) return {};
  auto* item = table_->item(row, 4);
  return item ? item->text() : QString();
}

bool Ft8Panel::activateDecode(int row) {
  if (row < 0 || static_cast<size_t>(row) >= rows_.size()) {
    emit statusMessage("请先在解码表选择一行", true);
    return false;
  }
  const RowMeta& meta = rows_[static_cast<size_t>(row)];
  const DecodeTarget target = parseDecodeTarget(meta.text);
  if (target.call.isEmpty()) {
    emit statusMessage(QString("无法从解码识别呼号：%1").arg(meta.text), true);
    return false;
  }
  quint64 hz = dial_hz_;
  if (hz != 0) {
    const int64_t target_hz =
        static_cast<int64_t>(hz) + std::llround(meta.df_hz);
    if (target_hz > 0) hz = static_cast<quint64>(target_hz);
  }
  emit decodeActivated(target.call, target.grid, hz, "FT8");
  emit statusMessage(QString("已送入主窗口：%1%2")
                         .arg(target.call,
                              hz == 0 ? QString()
                                      : QString(" · %1 MHz").arg(hz / 1e6, 0,
                                                                 'f', 6)),
                     false);
  return true;
}

}  // namespace radio::app
