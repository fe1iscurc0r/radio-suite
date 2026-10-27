// SPDX-License-Identifier: AGPL-3.0-or-later
// 对话框接线测试：验证 UI 动作确实走 RadioSession → CI-V → mock 的完整链路
// （离屏平台，不依赖桌面会话；TEST_CASE 名保持 ASCII）
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include <memory>

#include "app/cw_keyer.h"
#include "app/memory_channel.h"
#include "app/repeater_dialog.h"
#include "app/session.h"
#include "app/set_mode_dialog.h"
#include "core/transport_mock.h"

namespace {

QApplication& app() {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  static int argc = 1;
  static char name[] = "radio-ui-tests";
  static char* argv[] = {name, nullptr};
  static QApplication instance(argc, argv);
  return instance;
}

struct UiFixture {
  UiFixture() {
    app();
    transport = std::make_shared<radio::core::MockTransport>();
    session = std::make_unique<radio::app::RadioSession>(transport);
    session->start();
  }

  template <typename T>
  static T* find(QWidget* parent, const char* name) {
    auto* w = parent->findChild<T*>(name);
    REQUIRE(w != nullptr);
    return w;
  }

  std::shared_ptr<radio::core::MockTransport> transport;
  std::unique_ptr<radio::app::RadioSession> session;
};

}  // namespace

TEST_CASE("cw keyer dialog applies settings and sends text", "[ui][cw]") {
  UiFixture f;
  radio::app::CwKeyerDialog dlg(f.session.get());

  UiFixture::find<QSpinBox>(&dlg, "cw_wpm")->setValue(25);
  UiFixture::find<QSpinBox>(&dlg, "cw_pitch")->setValue(700);
  UiFixture::find<QCheckBox>(&dlg, "cw_break_in")->setChecked(true);
  UiFixture::find<QSpinBox>(&dlg, "cw_break_delay")->setValue(40);
  UiFixture::find<QPushButton>(&dlg, "cw_apply")->click();

  CHECK(f.transport->sim().key_speed_level ==
        radio::core::civ::wpmToKeySpeedLevel(25));
  CHECK(f.transport->sim().cw_pitch_level == radio::core::civ::hzToCwPitchLevel(700));
  CHECK(f.transport->sim().break_in);
  CHECK(f.transport->sim().break_in_delay_level == 40);

  UiFixture::find<QPlainTextEdit>(&dlg, "cw_text")->setPlainText("cq test");
  UiFixture::find<QPushButton>(&dlg, "cw_send")->click();
  CHECK(f.transport->sim().cw_text == "CQ TEST");
  CHECK(f.transport->sim().ptt);

  UiFixture::find<QPushButton>(&dlg, "cw_stop")->click();
  CHECK_FALSE(f.transport->sim().ptt);
  CHECK_FALSE(f.transport->sim().cw_sending);
  CHECK(f.session->gate().phase() == radio::core::PttPhase::Rx);
}

TEST_CASE("set mode dialog reads and writes a menu item", "[ui][menu]") {
  UiFixture f;
  radio::app::SetModeDialog dlg(f.session.get());

  auto* group = UiFixture::find<QSpinBox>(&dlg, "set_group");
  auto* item = UiFixture::find<QSpinBox>(&dlg, "set_item");
  auto* value = UiFixture::find<QLineEdit>(&dlg, "set_value");

  group->setValue(0x03);
  item->setValue(0x59);  // NB Depth
  value->setText("09");
  UiFixture::find<QPushButton>(&dlg, "set_write")->click();

  const uint16_t key = static_cast<uint16_t>((0x03 << 8) | 0x59);
  auto s = f.session->store().snapshot();
  REQUIRE(s.menu_values.count(key) == 1);
  CHECK(s.menu_values.at(key) == std::vector<uint8_t>{0x09});
  CHECK(UiFixture::find<QLabel>(&dlg, "set_readback")->text().contains("09"));

  UiFixture::find<QPushButton>(&dlg, "set_read")->click();
  CHECK(UiFixture::find<QLabel>(&dlg, "set_readback")->text().contains("回读"));
}

TEST_CASE("memory channel dialog writes and reads back a channel", "[ui][memory]") {
  UiFixture f;
  radio::app::MemoryChannelDialog dlg(f.session.get());

  UiFixture::find<QSpinBox>(&dlg, "mem_channel")->setValue(5);
  UiFixture::find<QDoubleSpinBox>(&dlg, "mem_freq")->setValue(7.05);
  UiFixture::find<QComboBox>(&dlg, "mem_mode")->setCurrentIndex(0);  // LSB
  UiFixture::find<QSpinBox>(&dlg, "mem_filter")->setValue(1);
  UiFixture::find<QLineEdit>(&dlg, "mem_name")->setText("TEST");
  UiFixture::find<QPushButton>(&dlg, "mem_write")->click();

  const auto& mem = f.transport->sim().memory;
  REQUIRE(mem.count(5) == 1);
  CHECK(mem.at(5).freq_hz == 7'050'000ULL);
  CHECK(mem.at(5).mode == radio::core::civ::Mode::LSB);
  CHECK(mem.at(5).name == "TEST");
  CHECK(UiFixture::find<QLabel>(&dlg, "mem_readback")->text().contains("CH5"));

  auto s = f.session->store().snapshot();
  REQUIRE(s.memory_channel.has_value());
  CHECK(s.memory_channel->channel == 5);

  UiFixture::find<QPushButton>(&dlg, "mem_read")->click();
  CHECK(UiFixture::find<QLabel>(&dlg, "mem_readback")->text().contains("7.050000"));
}

TEST_CASE("repeater dialog shows both frequencies and applies tone settings", "[ui][repeater]") {
  UiFixture f;
  radio::app::RepeaterDialog dlg(f.session.get());

  // 频差：DUP− + 600kHz → RX/TX 双频（TX = RX − 0.6）
  UiFixture::find<QComboBox>(&dlg, "rep_dir")->setCurrentIndex(1);
  UiFixture::find<QDoubleSpinBox>(&dlg, "rep_offset")->setValue(0.6);
  UiFixture::find<QPushButton>(&dlg, "rep_apply")->click();

  CHECK(f.transport->sim().duplex == 0x11);
  CHECK(f.transport->sim().offset_hz == 600'000U);
  CHECK(f.session->store().snapshot().offset_hz == 600'000U);
  CHECK(UiFixture::find<QLabel>(&dlg, "rep_rx")->text().contains("14.270000"));
  CHECK(UiFixture::find<QLabel>(&dlg, "rep_tx")->text().contains("13.670000"));

  // 模拟亚音：CTCSS 88.5Hz 写入并回读
  auto* tone_freq = UiFixture::find<QComboBox>(&dlg, "rep_tone_freq");
  const int ti = tone_freq->findData(885);
  REQUIRE(ti >= 0);
  tone_freq->setCurrentIndex(ti);
  UiFixture::find<QPushButton>(&dlg, "rep_tone_freq_apply")->click();
  CHECK(f.transport->sim().tone_decihz == 885);
  CHECK(f.session->store().snapshot().tone_decihz == 885);

  // 模式：TSQL（0x5D=02）
  auto* tone_mode = UiFixture::find<QComboBox>(&dlg, "rep_tone_mode");
  const int mi =
      tone_mode->findData(static_cast<int>(radio::core::civ::ToneSquelch::Tsql));
  REQUIRE(mi >= 0);
  tone_mode->setCurrentIndex(mi);
  UiFixture::find<QPushButton>(&dlg, "rep_tone_apply")->click();
  CHECK(f.transport->sim().tone_squelch == 0x02);

  // 数字亚音：DTCS 023
  auto* dtcs_code = UiFixture::find<QComboBox>(&dlg, "rep_dtcs_code");
  const int di = dtcs_code->findData(23);
  REQUIRE(di >= 0);
  dtcs_code->setCurrentIndex(di);
  UiFixture::find<QPushButton>(&dlg, "rep_dtcs_apply")->click();
  CHECK(f.transport->sim().dtcs_code == 23);
  CHECK(f.session->store().snapshot().dtcs_code == 23);
}
