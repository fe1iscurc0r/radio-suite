#include "app/MainWindow.h"
// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/MainWindow_actions.cpp — MainWindow 用户动作/持久化域（同一类的第四实现文件）：
// 调谐拒绝/发射阻断提示、手动输频、频段快捷、步进、本地记忆存取、自检台账收集、
// 音频设备设置、HamLog 记 QSO/ADIF 导出/QSO 列表刷新。

#include "app/audio_dialog.h"
#include "app/network_audio.h"
#include "app/wsjtx_client.h"
#include "app/wsjtx_panel.h"
#include "audio/audio_in.h"
#include "audio/audio_out.h"
#include "ui_common/app_settings.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSysInfo>

#include <string>

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

void MainWindow::onBandClicked() {
  auto* btn = qobject_cast<QPushButton*>(sender());
  if (!btn) return;
  const auto hz = btn->property("center_hz").toULongLong();
  if (!session_.tuneTo(hz))
    flashStatus(QString("频段 %1 中心频率被拒绝").arg(btn->text()), true);
  // 点击会把按钮自己切成 checked；以会话当前频率为准重算，别留假高亮
  syncBandHighlight(session_.store().snapshot().freq_hz);
}

void MainWindow::syncBandHighlight(uint64_t hz) {
  for (size_t i = 0; i < band_btns_.size(); ++i) {
    const bool on = hz >= band_low_[i] && hz < band_high_[i];
    if (band_btns_[i]->isChecked() == on) continue;
    QSignalBlocker guard(band_btns_[i]);
    band_btns_[i]->setChecked(on);
  }
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
  auto st = radio::ui::appSettings();
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
  auto st = radio::ui::appSettings();
  const auto freq = st.value(key + "/freq").toULongLong();
  const int mode = st.value(key + "/mode").toInt();
  if (freq == 0) {
    flashStatus(QString("%1 槽为空").arg(mem_slot_->currentText()), true);
    return;
  }
  session_.setMode(static_cast<radio::core::civ::Mode>(mode));
  session_.tuneTo(freq);
}

radio::app::ReportInput MainWindow::selfCheckInput() {
  // 台账只收集「现场能读到的事实」，判定与措辞都在 buildDeviceReport 里，
  // 所以这里不许出现任何猜测性文案。
  radio::app::ReportInput in;
  in.platform = QSysInfo::productType();
  in.storage_dir = radio::ui::appSettings().fileName();  // 桌面=注册表键，安卓=真能写的 ini 路径
  in.rig_udp_built = radio::app::udpLinkBuilt();
  in.rig_tcp_built = radio::app::tcpLinkBuilt();
  in.rig_bridge_available = radio::app::bridgeAvailable();
#ifdef RADIO_SUITE_HAVE_SERIALPORT
  in.rig_serial_built = true;
#endif
#ifdef RADIO_SUITE_HAVE_BUS
  in.rig_http_bus_built = true;
#endif
  in.link_name = QString::fromLatin1(radio::app::linkName(link_.link));
  in.rig_host = QString::fromStdString(link_.host);
  in.rig_link_alive = session_.linkAlive();
  in.rig_reply_age_ms = session_.replyAgeMs();
  if (link_.link == radio::app::Link::Tcp && !link_.host.empty())
    in.rig_host += QStringLiteral(":%1").arg(link_.port);
  if (wsjtx_panel_ && wsjtx_panel_->client()) {
    auto* client = wsjtx_panel_->client();
    in.wsjtx_listening = client->isListening();
    in.wsjtx_group = client->group();
    in.wsjtx_port = client->port();
    in.multicast_lock_held = client->multicastLockHeld();
    in.wsjtx_last_error = client->lastError();
  }
  radio::audio::AudioSink sink_enum;  // 现场枚举，USB 声卡插拔后不用重启
  in.audio_output_devices = static_cast<int>(sink_enum.outputDeviceIds().size());
  in.net_audio_active = network_audio_ && network_audio_->active();
#ifdef RADIO_SUITE_HAVE_LOGBOOK
  in.logbook_available = hamlog_.available();
  in.logbook_path = radio::logbook::HamLogDb::defaultDbPath();
  in.logbook_error = hamlog_.lastError();
  in.logbook_has_rows = !hamlog_.recent(1).empty();
#endif
  return in;
}

void MainWindow::onAudioSettings() {
  // 设备表现场枚举（USB 插拔后无需重启程序即可看到新设备）
  radio::audio::AudioIn mic_enum;
  radio::audio::AudioSink sink_enum;
  std::vector<radio::app::AudioSettingsDialog::Device> ins, outs;
  {
    const auto ids = mic_enum.inputDeviceIds();
    const auto names = mic_enum.deviceNames();
    for (size_t i = 0; i < ids.size() && i < names.size(); ++i)
      ins.push_back({static_cast<int>(ids[i]), QString::fromStdString(names[i])});
  }
  {
    const auto ids = sink_enum.outputDeviceIds();
    const auto names = sink_enum.outputDeviceNames();
    for (size_t i = 0; i < ids.size() && i < names.size(); ++i)
      outs.push_back({static_cast<int>(ids[i]), QString::fromStdString(names[i])});
  }

  radio::app::AudioSettingsDialog dlg(this);
  dlg.setDevices(ins, outs, network_audio_ ? network_audio_->inputDevice() : -1,
                 network_audio_ ? network_audio_->outputDevice() : -1);
  if (network_audio_ && network_audio_->active())
    dlg.setStatsProvider([this] { return network_audio_->stats(); });
  if (dlg.exec() != QDialog::Accepted) return;

  if (!network_audio_) return;
  const int in_id = dlg.chosenInputId();
  const int out_id = dlg.chosenOutputId();
  {
    auto st = radio::ui::appSettings();
    st.setValue("audio/in_dev", in_id);
    st.setValue("audio/out_dev", out_id);
  }
  network_audio_->setDevices(in_id, out_id);
  if (!network_audio_->active()) {
    flashStatus("音频设备已保存：下次打开「网络音频」生效", false);
    return;
  }
  // 正在跑：按新设备重开（失败则按钮回弹，避免"显示 ON 但没声"）
  network_audio_->stop();
  std::string err;
  if (network_audio_->start(&err)) {
    flashStatus("音频设备已切换并重新打开", false);
  } else {
    QSignalBlocker guard(audio_btn_);
    audio_btn_->setChecked(false);
    flashStatus(QString("新设备打开失败，已退回关闭：%1")
                    .arg(QString::fromStdString(err)),
                true);
  }
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
