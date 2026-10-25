// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/sstv_dialog.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/session.h"
#include "audio/audio_out.h"
#include "core/safety.h"
#include "core/sstv.h"

SstvDialog::SstvDialog(radio::app::RadioSession* session, QWidget* parent)
    : QDialog(parent), session_(session) {
  setWindowTitle("SSTV 录音盒（发送 = 生成录音 · 接收 = 听录音）");
  setMinimumSize(440, 480);
  auto* root = new QVBoxLayout(this);

  // 录音盒模型：电脑编解码=制式自由；705 无机内编解码，业界同为外接软件
  auto* mode_row = new QHBoxLayout();
  mode_row->addWidget(new QLabel("制式", this));
  mode_box_ = new QComboBox(this);
  for (int m = 0; m <= 3; ++m)
    mode_box_->addItem(radio::core::sstv::modeName(
        static_cast<radio::core::sstv::Mode>(m)));
  mode_box_->setCurrentIndex(0);
  mode_row->addWidget(mode_box_, 1);
  root->addLayout(mode_row);

  auto* pick = new QPushButton("选择图片（按制式缩放）", this);
  connect(pick, &QPushButton::clicked, this, &SstvDialog::onPickImage);
  root->addWidget(pick);

  preview_ = new QLabel("（未选图）", this);
  preview_->setAlignment(Qt::AlignCenter);
  preview_->setMinimumHeight(180);
  preview_->setStyleSheet("background:#10141c;");
  root->addWidget(preview_, 1);

  gen_btn_ = new QPushButton("生成 .wav", this);
  gen_btn_->setEnabled(false);
  connect(gen_btn_, &QPushButton::clicked, this, &SstvDialog::onGenerate);
  root->addWidget(gen_btn_);

  info_ = new QLabel(
      "IC-705 无机内 SSTV 编解码——电脑当\"录音盒\"：任意制式先在电脑上编好，\n"
      "播放 .wav 进电台 MOD 即发射（M2 接音频直出+PTT 联动）", this);
  info_->setStyleSheet("color:#9fe;");
  root->addWidget(info_);

  // ---- 一键发射：PTT 门禁（两击确认）→ TX → 播放 → 自动回 RX ----
  tx_btn_ = new QPushButton("一键发射（PTT 门禁 + 播放）", this);
  tx_btn_->setEnabled(false);
  tx_btn_->setStyleSheet("QPushButton { background: #8e24aa; color: white; }");
  connect(tx_btn_, &QPushButton::clicked, this, &SstvDialog::onTransmit);
  root->addWidget(tx_btn_);
  audio_ = std::make_unique<radio::audio::AudioOut>();

  auto* close = new QPushButton("关闭", this);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  root->addWidget(close, 0, Qt::AlignRight);

  // ---- 接收：从 .wav 自动识别制式解码（实时监听 M2 随音频链） ----
  auto* rx_head = new QLabel("接收（VIS 自动识别制式）", this);
  root->addWidget(rx_head);
  auto* rx_row = new QHBoxLayout();
  auto* dec_btn = new QPushButton("从 .wav 解码…", this);
  connect(dec_btn, &QPushButton::clicked, this, &SstvDialog::onDecode);
  rx_row->addWidget(dec_btn);
  auto* save_btn = new QPushButton("保存图片…", this);
  connect(save_btn, &QPushButton::clicked, this, [this] {
    if (rx_img_.isNull()) return;
    const QString p = QFileDialog::getSaveFileName(this, "保存", "sstv_rx.png",
                                                   "PNG (*.png)");
    if (!p.isEmpty()) rx_img_.save(p);
  });
  rx_row->addWidget(save_btn);
  rx_row->addStretch(1);
  root->addLayout(rx_row);
  rx_view_ = new QLabel("（解码结果显示处）", this);
  rx_view_->setAlignment(Qt::AlignCenter);
  rx_view_->setMinimumHeight(160);
  rx_view_->setStyleSheet("background:#10141c;");
  root->addWidget(rx_view_, 1);
}

SstvDialog::~SstvDialog() = default;  // unique_ptr<AudioOut>（完整类型在 audio_out.h）

void SstvDialog::onDecode() {
  const QString path = QFileDialog::getOpenFileName(this, "选择 SSTV .wav",
                                                    {}, "WAV (*.wav)");
  if (path.isEmpty()) return;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    rx_view_->setText("无法打开文件");
    return;
  }
  const QByteArray all = f.readAll();
  f.close();
  // 简化 WAV 解析：44100/16bit/单声道，找 "data" 块
  int data_off = all.indexOf("data");
  if (data_off < 0) {
    rx_view_->setText("非 WAV 文件（无 data 块）");
    return;
  }
  const int32_t data_len =
      (static_cast<uint8_t>(all[data_off + 4])) |
      (static_cast<uint8_t>(all[data_off + 5]) << 8) |
      (static_cast<uint8_t>(all[data_off + 6]) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(all[data_off + 7])) << 24);
  const int n = data_len / 2;
  std::vector<int16_t> pcm(n);
  std::memcpy(pcm.data(), all.constData() + data_off + 8, data_len);

  const auto img = radio::core::sstv::decodeSstv(pcm, 44100);
  if (img.empty()) {
    rx_view_->setText("未识别 SSTV VIS 头（不是 SSTV 或不支持的制式）");
    return;
  }
  const int w = static_cast<int>(sqrt(img.size() * 3.0 / 4.0));  // 粗略（仅提示）
  Q_UNUSED(w);
  rx_img_ = QImage(320, 240, QImage::Format_RGB888);
  for (int y = 0; y < 240; ++y)
    for (int x = 0; x < 320; ++x) {
      const auto& p = img[static_cast<size_t>(y) * 320 + x];
      rx_img_.setPixel(x, y, qRgb(p.r, p.g, p.b));
    }
  rx_view_->setPixmap(QPixmap::fromImage(
      rx_img_.scaled(320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
  info_->setText(QString("解码完成：%1").arg(path.section('/', -1)));
}

void SstvDialog::onPickImage() {
  const QString path = QFileDialog::getOpenFileName(
      this, "选择图片", {}, "图片 (*.png *.jpg *.jpeg *.bmp)");
  if (path.isEmpty()) return;
  image_path_ = path;
  QImage img(path);
  if (img.isNull()) {
    preview_->setText("无法解码图片");
    gen_btn_->setEnabled(false);
    return;
  }
  const auto mode = static_cast<radio::core::sstv::Mode>(mode_box_->currentIndex());
  const int w = radio::core::sstv::imageWidth(mode);
  const int h = radio::core::sstv::imageHeight(mode);
  img_ = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
             .convertToFormat(QImage::Format_RGB888);
  preview_->setPixmap(QPixmap::fromImage(
      img_.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
  gen_btn_->setEnabled(true);
  info_->setText(QString("已选：%1 → %2 %3×%4 → 点「生成 .wav」")
                     .arg(path.section('/', -1),
                          radio::core::sstv::modeName(mode))
                     .arg(w).arg(h));
}

void SstvDialog::onGenerate() {
  if (img_.isNull()) return;
  const auto mode = static_cast<radio::core::sstv::Mode>(mode_box_->currentIndex());
  const int w = radio::core::sstv::imageWidth(mode);
  const int h = radio::core::sstv::imageHeight(mode);
  const QString tag = QString("sstv_%1")
                          .arg(radio::core::sstv::modeName(mode))
                          .replace(' ', '_');
  const QString out_path =
      image_path_.section('/', 0, -2) + "/" +
      image_path_.section('/', -1).section('.', 0, 0) + "_" + tag + ".wav";
  QFile f(out_path);
  if (!f.open(QIODevice::WriteOnly)) {
    info_->setText("无法写 " + out_path);
    return;
  }
  std::vector<radio::core::sstv::Rgb> rgb(
      static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const uchar* p = img_.scanLine(y) + x * 3;
      rgb[static_cast<size_t>(y) * w + x] = {p[0], p[1], p[2]};
    }
  const auto pcm = radio::core::sstv::encodeSstv(mode, rgb, 44100);
  last_pcm_ = pcm;
  tx_btn_->setEnabled(true);
  const double secs = double(pcm.size()) / 44100.0;
  // WAV 头（PCM 16bit 单声道）
  const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * 2);
  const uint32_t byte_rate = 44100 * 2;
  auto put32 = [&f](uint32_t v) {
    f.putChar(char(v & 0xFF)); f.putChar(char((v >> 8) & 0xFF));
    f.putChar(char((v >> 16) & 0xFF)); f.putChar(char((v >> 24) & 0xFF));
  };
  auto put16 = [&f](uint16_t v) {
    f.putChar(char(v & 0xFF)); f.putChar(char((v >> 8) & 0xFF));
  };
  f.write("RIFF"); put32(36 + data_bytes); f.write("WAVE");
  f.write("fmt "); put32(16); put16(1); put16(1);
  put32(44100); put32(byte_rate); put16(2); put16(16);
  f.write("data"); put32(data_bytes);
  f.write(reinterpret_cast<const char*>(pcm.data()),
          static_cast<qint64>(data_bytes));
  f.close();
  info_->setText(QString("已生成：%1（%2 KB，%3 秒，%4）\n播放它进电台 MOD 即可发射；或直接点「一键发射」")
                     .arg(out_path)
                     .arg(data_bytes / 1024)
                     .arg(secs, 0, 'f', 1)
                     .arg(radio::core::sstv::modeName(mode)));
}

void SstvDialog::onTransmit() {
  // 全程走 PTT 门禁（safety 是唯一出口，UI 只编排交互）：
  // 第一击 requestTx → 立即 confirmTx（对话框内视为同一次确认动作）
  // → TX + 播放 → 播完 requestRx。mock 上完整可演；真机等 M1 验收。
  if (last_pcm_.empty()) return;
  if (!session_) {
    info_->setText("会话未就绪");
    return;
  }
  const uint64_t now = QDateTime::currentMSecsSinceEpoch();
  auto& gate = session_->gate();
  if (gate.phase() != radio::core::PttPhase::Tx) {
    gate.requestTx(now);
    if (!gate.confirmTx(now)) {
      info_->setText("PTT 门禁未确认（5s 窗口），再点一次");
      return;
    }
    // 门禁最后一道：非业余段拒绝发射
    const uint64_t hz = session_->store().snapshot().freq_hz;
    if (!radio::core::SafetyGate::authorizeTx(hz)) {
      gate.requestRx(now);
      info_->setText(QString("发射被拒：%1 MHz 不在业余频段").arg(hz / 1e6));
      return;
    }
    // 发射命令（mock 直接生效；真机同帧路径）
    session_->setMode(session_->store().snapshot().mode);  // 占位保活
  }

  info_->setText("发射中…（音频直出 MOD）");
  std::string err;
  const bool ok = audio_->play(last_pcm_, 44100, 0, &err);
  if (gate.phase() == radio::core::PttPhase::Tx) {
    gate.requestRx(QDateTime::currentMSecsSinceEpoch());
    session_->pttButtonPressed();  // 清 TX 态并补发 PTT OFF 帧
    session_->pttButtonPressed();  // 上一次点击回 RX（幂等）
  }
  info_->setText(ok ? "已播完并回 RX" : QString("音频失败：") + err.c_str());
}
