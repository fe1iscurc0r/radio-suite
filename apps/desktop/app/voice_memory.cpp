// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/voice_memory.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

#include "app/session.h"
#include "audio/audio_in.h"
#include "audio/audio_out.h"
#include "core/safety.h"

namespace {
constexpr uint32_t kRate = 48000;
constexpr int kMaxSeconds = 60;

void writeWav(const QString& path, const std::vector<int16_t>& pcm) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return;
  const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * 2);
  auto put32 = [&f](uint32_t v) {
    for (int i = 0; i < 4; ++i) f.putChar(char((v >> (8 * i)) & 0xFF));
  };
  auto put16 = [&f](uint16_t v) {
    f.putChar(char(v & 0xFF));
    f.putChar(char((v >> 8) & 0xFF));
  };
  f.write("RIFF"); put32(36 + data_bytes); f.write("WAVE");
  f.write("fmt "); put32(16); put16(1); put16(1);
  put32(kRate); put32(kRate * 2); put16(2); put16(16);
  f.write("data"); put32(data_bytes);
  f.write(reinterpret_cast<const char*>(pcm.data()),
          static_cast<qint64>(data_bytes));
}

std::vector<int16_t> readWav(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  const QByteArray all = f.readAll();
  const int off = all.indexOf("data");
  if (off < 0 || off + 8 > all.size()) return {};
  const int32_t len =
      static_cast<uint8_t>(all[off + 4]) |
      (static_cast<uint8_t>(all[off + 5]) << 8) |
      (static_cast<uint8_t>(all[off + 6]) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(all[off + 7])) << 24);
  // 截断文件按实际剩余字节算，避免越界 memcpy
  const int available = all.size() - (off + 8);
  const int n = std::max(0, std::min(static_cast<int>(len), available)) / 2;
  std::vector<int16_t> pcm(static_cast<size_t>(n));
  if (n > 0) std::memcpy(pcm.data(), all.constData() + off + 8,
                         static_cast<size_t>(n) * sizeof(int16_t));
  return pcm;
}
}  // namespace

VoiceMemoryDialog::VoiceMemoryDialog(radio::app::RadioSession* session,
                                     QWidget* parent)
    : QDialog(parent), session_(session) {
  setWindowTitle("Voice Memory（语音记忆）");
  setMinimumSize(380, 360);
  auto* root = new QVBoxLayout(this);

  list_ = new QListWidget(this);
  root->addWidget(list_, 1);

  auto* row = new QHBoxLayout();
  rec_btn_ = new QPushButton("● 录音", this);
  rec_btn_->setStyleSheet("QPushButton { background: #c62828; color: white; }");
  connect(rec_btn_, &QPushButton::clicked, this, &VoiceMemoryDialog::onRecordToggle);
  row->addWidget(rec_btn_);
  play_btn_ = new QPushButton("▶ 播放", this);
  connect(play_btn_, &QPushButton::clicked, this, &VoiceMemoryDialog::onPlay);
  row->addWidget(play_btn_);
  tx_btn_ = new QPushButton("发射选中", this);
  tx_btn_->setStyleSheet("QPushButton { background: #8e24aa; color: white; }");
  connect(tx_btn_, &QPushButton::clicked, this, &VoiceMemoryDialog::onTransmit);
  row->addWidget(tx_btn_);
  root->addLayout(row);

  status_ = new QLabel("PTT 门禁编排发射（同 SSTV）：确认 → TX 播放 → 回 RX", this);
  status_->setStyleSheet("color:#9fe;");
  root->addWidget(status_);

  auto* close = new QPushButton("关闭", this);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  root->addWidget(close, 0, Qt::AlignRight);

  audio_in_ = std::make_unique<radio::audio::AudioIn>();
  audio_out_ = std::make_unique<radio::audio::AudioOut>();
  refreshList();
}

VoiceMemoryDialog::~VoiceMemoryDialog() {
  audio_in_->stop();
  audio_out_->wait();
  if (tx_active_) {
    tx_active_ = false;
    if (session_) session_->endScriptedTx();
  }
}

QString VoiceMemoryDialog::voiceDir() const {
  // 音频文件放仓库 tools/voice/（随仓管理，不放系统目录）
  const QString dir = QDir(QCoreApplication::applicationDirPath())
                          .absoluteFilePath("../../tools/voice");
  QDir().mkpath(dir);
  return QDir(dir).absolutePath();
}

void VoiceMemoryDialog::refreshList() {
  list_->clear();
  const auto entries =
      QDir(voiceDir(), "vm_*.wav", QDir::Name, QDir::Files).entryList();
  for (const QString& f : entries)
    list_->addItem(f.section('.', 0, 0));
}

void VoiceMemoryDialog::onRecordToggle() {
  if (!recording_) {
    const auto ids = audio_in_->inputDeviceIds();
    if (ids.empty()) {
      status_->setText("无输入设备");
      return;
    }
    rec_path_ = QString("%1/vm_%2.wav")
                    .arg(voiceDir())
                    .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_hhmmss"));
    rec_pcm_.clear();
    audio_in_->start(ids[0], [this](std::span<const int16_t> frame) {
      if (!recording_) return;
      rec_pcm_.insert(rec_pcm_.end(), frame.begin(), frame.end());
      if (rec_pcm_.size() >= kRate * kMaxSeconds) recording_ = false;  // 60s 上限
    });
    recording_ = true;
    rec_btn_->setText("■ 停止");
    status_->setText("录音中（最长 60s）…");
  } else {
    recording_ = false;
    audio_in_->stop();
    writeWav(rec_path_, rec_pcm_);
    rec_btn_->setText("● 录音");
    refreshList();
    status_->setText(QString("已保存 %1（%2s）")
                         .arg(rec_path_.section('/', -1))
                         .arg(rec_pcm_.size() / double(kRate), 0, 'f', 1));
  }
}

void VoiceMemoryDialog::onPlay() {
  if (!list_->currentItem()) return;
  if (audio_out_->playing()) {
    status_->setText("播放中…");
    return;
  }
  const QString path = voiceDir() + "/" + list_->currentItem()->text() + ".wav";
  const auto pcm = readWav(path);
  if (pcm.empty()) {
    status_->setText("文件不可读");
    return;
  }
  const int device = audio_out_->defaultOutputDevice();
  if (device < 0) {
    status_->setText("无可用输出设备");
    return;
  }
  status_->setText("播放中…");
  play_btn_->setEnabled(false);
  const bool started = audio_out_->playAsync(
      pcm, kRate, static_cast<uint32_t>(device),
      [this](bool ok, std::string err) {
        QMetaObject::invokeMethod(
            this,
            [this, ok, err] {
              play_btn_->setEnabled(true);
              status_->setText(ok ? "播放完成"
                                  : QString("播放失败：") + err.c_str());
            },
            Qt::QueuedConnection);
      });
  if (!started) {
    play_btn_->setEnabled(true);
    status_->setText("播放线程忙");
  }
}

void VoiceMemoryDialog::onTransmit() {
  if (!list_->currentItem() || tx_active_) return;
  const auto pcm = readWav(voiceDir() + "/" + list_->currentItem()->text() + ".wav");
  if (pcm.empty()) return;
  if (!session_ || !session_->beginScriptedTx()) {
    status_->setText("发射被门禁拒绝");
    return;
  }
  const int device = audio_out_->defaultOutputDevice();
  if (device < 0) {
    session_->endScriptedTx();
    status_->setText("无可用输出设备");
    return;
  }
  // 录音最长 60s：后台播放，GUI 不冻结；播完在 GUI 线程回 RX
  tx_active_ = true;
  tx_btn_->setEnabled(false);
  status_->setText("发射中…（Voice Memory → MOD，播完自动回 RX）");
  const bool started = audio_out_->playAsync(
      pcm, kRate, static_cast<uint32_t>(device),
      [this](bool ok, std::string err) {
        QMetaObject::invokeMethod(
            this,
            [this, ok, err] {
              tx_active_ = false;
              tx_btn_->setEnabled(true);
              if (session_) session_->endScriptedTx();
              status_->setText(ok ? "已播完并回 RX"
                                  : QString("音频失败：") + err.c_str());
            },
            Qt::QueuedConnection);
      });
  if (!started) {
    tx_active_ = false;
    tx_btn_->setEnabled(true);
    session_->endScriptedTx();
    status_->setText("播放线程忙，已回 RX");
  }
}
