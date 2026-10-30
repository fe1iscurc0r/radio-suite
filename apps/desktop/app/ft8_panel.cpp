// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/ft8_panel.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

#include "app/decode_target.h"
#include "ft8/ft8_tx.h"

namespace radio::app {
namespace {

constexpr int kMaxRows = 500;  // 与 WSJT-X 面板解码表同量级
constexpr int64_t kTxLeadMs = 500;  // 槽起点前留给 PTT 键控与音频设备启动
constexpr int64_t kTxFireLateWarnMs = 250;  // 起播晚于槽起点这么多就算「没对齐」
constexpr uint32_t kTxRateHz = 48000;

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

  // 发射：文本 + 音频频偏 + 槽奇偶 + 一个会变身（排期 / 取消）的按钮
  auto* txrow = new QHBoxLayout();
  txrow->addWidget(new QLabel("文本", this));
  msg_edit_ = new QLineEdit(this);
  msg_edit_->setPlaceholderText("CQ BG5GXO PM01");
  txrow->addWidget(msg_edit_, 1);
  txrow->addWidget(new QLabel("音频频偏", this));
  f0_spin_ = new QSpinBox(this);
  f0_spin_->setRange(200, 2700);
  f0_spin_->setSingleStep(50);
  f0_spin_->setSuffix(" Hz");
  f0_spin_->setValue(1000);
  txrow->addWidget(f0_spin_);
  parity_box_ = new QComboBox(this);
  parity_box_->addItems({"任意时隙", "偶数时隙", "奇数时隙"});
  txrow->addWidget(parity_box_);
  tx_btn_ = new QPushButton("定时发射", this);
  txrow->addWidget(tx_btn_);
  tx_info_ = new QLabel(this);
  tx_info_->setMinimumWidth(210);
  txrow->addWidget(tx_info_);
  root->addLayout(txrow);
  updateTxLabel(0);

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

  connect(tx_btn_, &QPushButton::clicked, this, &Ft8Panel::onTxClicked);
  tx_timer_ = new QTimer(this);
  tx_timer_->setSingleShot(true);
  connect(tx_timer_, &QTimer::timeout, this, [this] { fireTransmit(); });

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

bool Ft8Panel::scheduleTransmit(const QString& text, float f0_hz, int parity,
                                int64_t now_ms) {
  if (tx_pending_) {
    tx_info_->setText("已有一次发射在等待，先取消");
    return false;
  }
  if (text.trimmed().isEmpty()) {
    tx_info_->setText("文本为空");
    return false;
  }
  std::vector<int16_t> pcm;
  try {
    pcm = radio::ft8::renderTxPcm(text.toStdString(), f0_hz);
  } catch (const std::exception& e) {
    tx_info_->setText("文本无法按 FT8 打包");
    emit statusMessage(QString("FT8 发射取消：%1").arg(e.what()), true);
    return false;
  }
  const int64_t start = radio::ft8::nextTxSlotStart(now_ms, kTxLeadMs, parity);
  tx_pcm_ = std::move(pcm);
  tx_start_ms_ = start;
  tx_pending_ = true;
  tx_timer_->start(static_cast<int>(start - now_ms));
  updateTxLabel(now_ms);
  emit statusMessage(
      QString("FT8 已排期：%1 UTC 起发射（双击前不会自动重发）")
          .arg(QDateTime::fromMSecsSinceEpoch(start).toUTC().toString("hh:mm:ss")),
      false);
  return true;
}

void Ft8Panel::cancelTransmit() {
  tx_timer_->stop();
  tx_pending_ = false;
  tx_pcm_.clear();
  tx_start_ms_ = 0;
  updateTxLabel(static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch()));
}

bool Ft8Panel::fireTransmit(int64_t now_ms) {
  if (!tx_pending_) return false;
  const int64_t start = tx_start_ms_;
  if (now_ms <= 0) now_ms = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
  // QTimer 晚了说明 GUI 线程被堵住（模态框/重活）：整槽波形会整体偏离槽起点，
  // 拦不住也不该悄悄发出去，把偏差报出来让操作者知道这一发不干净
  const int64_t late = now_ms - start;
  tx_pending_ = false;
  std::vector<int16_t> pcm;
  pcm.swap(tx_pcm_);
  if (!tx_) {
    tx_info_->setText("未接发射出口（需要主窗音频链路）");
    emit statusMessage("FT8 发射未开始：本面板没有接上音频出口", true);
    return false;
  }
  if (!tx_(std::move(pcm), kTxRateHz)) {
    tx_info_->setText("发射被拒绝（门禁或非业余段）");
    emit statusMessage("FT8 发射未开始：门禁拒绝或无输出设备", true);
    return false;
  }
  tx_info_->setText(QString("%1 UTC 起整槽发射").arg(
      QDateTime::fromMSecsSinceEpoch(start).toUTC().toString("hh:mm:ss")));
  emit statusMessage(late > kTxFireLateWarnMs
                         ? QString("FT8 发射中（比槽起点晚 %1 ms，这一发对不齐）")
                               .arg(late)
                         : "FT8 发射中（15 s 整槽，播完自动回 RX）",
                     late > kTxFireLateWarnMs);
  return true;
}

void Ft8Panel::onTxClicked() {
  const int64_t now = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
  if (tx_pending_) {
    cancelTransmit();
    return;
  }
  const int parity = parity_box_->currentIndex() - 1;  // 0=任意 → -1
  scheduleTransmit(msg_edit_->text(),
                   static_cast<float>(f0_spin_->value()), parity, now);
}

void Ft8Panel::updateTxLabel(int64_t now_ms) {
  if (!tx_pending_) {
    tx_btn_->setText("定时发射");
    tx_info_->setText("等到下一个时隙起点整槽播出（15 s）");
    return;
  }
  tx_btn_->setText("取消发射");
  const double left = static_cast<double>(tx_start_ms_ - now_ms) / 1000.0;
  tx_info_->setText(
      QString("%1 UTC 起（%2 s 后）")
          .arg(QDateTime::fromMSecsSinceEpoch(tx_start_ms_)
                   .toUTC()
                   .toString("hh:mm:ss"))
          .arg(left, 0, 'f', 1));
}

void Ft8Panel::refreshTick() {
  // 排期倒计时跟着 tick 走：不解码时也要能看到还剩几秒（下面 decoder 未跑会早退）
  if (tx_pending_) {
    updateTxLabel(static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch()));
  }
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
