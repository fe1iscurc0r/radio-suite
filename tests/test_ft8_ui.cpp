// SPDX-License-Identifier: AGPL-3.0-or-later
// FT8 本机解码面板：结果注入 / 双击回填 / PCM→面板端到端（离屏 UI）
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <cmath>
#include <functional>
#include <vector>

#include "app/ft8_panel.h"
#include "ft8/ft8_codec.h"

namespace {

QApplication& uiApp() {
  static int argc = 1;
  static char name[] = "ft8-ui-tests";
  static char* argv[] = {name, nullptr};
  static QApplication app(argc, argv);
  return app;
}

bool waitUntil(const std::function<bool()>& pred, int timeout_ms = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!pred() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(5);
  }
  return pred();
}

radio::ft8::SlotResult slotWith(const char* text, float df_hz) {
  radio::ft8::SlotResult result;
  result.utc_ms = 86'400'000;  // 一天整
  radio::ft8::DecodedMessage msg;
  msg.text = text;
  msg.snr_db = -12.0f;
  msg.dt_s = 0.1f;
  msg.df_hz = df_hz;
  msg.score = 30;
  result.messages.push_back(msg);
  result.decode_ms = 12.0;
  return result;
}

}  // namespace

TEST_CASE("ft8 panel: injected slot fills the table and activates a target",
          "[ft8][ui]") {
  uiApp();
  radio::app::Ft8Panel panel;
  panel.setDialHz(14'074'000);

  panel.appendResult(slotWith("CQ BG5GXO PM01", 1735.0f));
  REQUIRE(panel.decodeRowCount() == 1);
  REQUIRE(panel.decodeText(0) == "CQ BG5GXO PM01");

  QString call, grid;
  quint64 hz = 0;
  QObject::connect(
      &panel, &radio::app::Ft8Panel::decodeActivated,
      [&](const QString& c, const QString& g, quint64 f, const QString& mode) {
        call = c;
        grid = g;
        hz = f;
        REQUIRE(mode == "FT8");
      });

  REQUIRE(panel.activateDecode(0));
  REQUIRE(call == "BG5GXO");
  REQUIRE(grid == "PM01");
  REQUIRE(hz == 14'074'000ull + 1735ull);

  // 无呼号的行要拒绝（"73" 之类）
  panel.appendResult(slotWith("TU 73", 500.0f));
  REQUIRE(panel.decodeRowCount() == 2);
  REQUIRE_FALSE(panel.activateDecode(1));
}

TEST_CASE("ft8 panel: decode table keeps only the newest rows", "[ft8][ui]") {
  uiApp();
  radio::app::Ft8Panel panel;
  panel.setDialHz(14'074'000);
  for (int i = 0; i < 600; ++i)
    panel.appendResult(slotWith("CQ BG5GXO PM01", 1000.0f + static_cast<float>(i)));
  REQUIRE(panel.decodeRowCount() == 500);

  // 淘汰的是最老的，且行元数据必须跟着表一起前移（否则激活的是另一行）
  quint64 hz = 0;
  QObject::connect(&panel, &radio::app::Ft8Panel::decodeActivated,
                   [&](const QString&, const QString&, quint64 f, const QString&) {
                     hz = f;
                   });
  REQUIRE(panel.activateDecode(0));
  REQUIRE(hz == 14'074'000ull + 1100ull);
  REQUIRE(panel.activateDecode(499));
  REQUIRE(hz == 14'074'000ull + 1599ull);
}

TEST_CASE("ft8 panel: rx pcm flows through slot decoder into the table",
          "[ft8][ui]") {
  uiApp();
  radio::app::Ft8Panel panel;
  panel.setDialHz(14'074'000);

  const auto tones = radio::ft8::encodeTones("CQ BG5GXO PM01");
  const auto audio12k = radio::ft8::synthesize(tones, 1000.0f);
  REQUIRE(audio12k.size() == radio::ft8::kSlotSamples12k);

  // 12k → 48k 零阶保持，按 20ms 块喂（与 50003 下行节奏一致）
  std::vector<int16_t> pcm;
  pcm.reserve(audio12k.size() * 4);
  for (float v : audio12k) {
    const auto s = static_cast<int16_t>(std::lround(v * 12000.0f));
    for (int k = 0; k < 4; ++k) pcm.push_back(s);
  }

  panel.startDecoding(0);  // 测试：从时隙边界立刻开始
  REQUIRE(panel.decoding());
  for (size_t i = 0; i < pcm.size(); i += 960) {
    const size_t n = std::min<size_t>(960, pcm.size() - i);
    panel.feedRxPcm(std::span<const int16_t>(pcm.data() + i, n));
  }

  REQUIRE(waitUntil([&] { return panel.decodeRowCount() > 0; }, 120000));
  REQUIRE(panel.decodeText(0) == "CQ BG5GXO PM01");

  panel.stopDecoding();
  REQUIRE_FALSE(panel.decoding());
  const int rows = panel.decodeRowCount();
  panel.feedRxPcm(std::span<const int16_t>(pcm.data(), 960));  // 停了就不再收
  REQUIRE(panel.decodeRowCount() == rows);
}

TEST_CASE("ft8 panel: tx schedules the slot then hands the exit one full slot",
          "[ft8][ui]") {
  uiApp();
  radio::app::Ft8Panel panel;

  std::vector<int16_t> got;
  uint32_t rate = 0;
  int fires = 0;
  panel.setTxController([&](std::vector<int16_t> pcm, uint32_t r) {
    got = std::move(pcm);
    rate = r;
    ++fires;
    return true;
  });

  // 排期阶段只算时间不许起播：到点前发射出口必须还是 0 次
  REQUIRE(panel.scheduleTransmit("CQ BG5GXO PM01", 1000.0f, -1, 10'000));
  REQUIRE(panel.txScheduled());
  REQUIRE(panel.txStartUtcMs() == 15'000);
  REQUIRE(fires == 0);

  REQUIRE(panel.fireTransmit(15'000));
  REQUIRE(fires == 1);
  REQUIRE(rate == 48'000);
  REQUIRE(got.size() == 15'000 * 48);
  REQUIRE_FALSE(panel.txScheduled());
  REQUIRE_FALSE(panel.fireTransmit(30'000));  // 一发只播一遍，不自动重发
  REQUIRE(fires == 1);
}

TEST_CASE("ft8 panel: tx rejects bad input, double booking and refusals",
          "[ft8][ui]") {
  uiApp();
  radio::app::Ft8Panel panel;
  int fires = 0;
  panel.setTxController([&](std::vector<int16_t>, uint32_t) {
    ++fires;
    return true;
  });

  REQUIRE_FALSE(panel.scheduleTransmit("   ", 1000.0f, -1, 10'000));
  REQUIRE_FALSE(panel.scheduleTransmit("this text is far too long for ft8", 1000.0f,
                                       -1, 10'000));
  REQUIRE_FALSE(panel.txScheduled());
  REQUIRE(fires == 0);

  // parity 走 UI 的偶/奇槽语义：0=偶（:00 :30），1=奇
  REQUIRE(panel.scheduleTransmit("CQ BG5GXO PM01", 1000.0f, 0, 10'000));
  REQUIRE(panel.txStartUtcMs() == 30'000);
  REQUIRE_FALSE(panel.scheduleTransmit("CQ BG5GXO PM01", 1000.0f, -1, 10'000));
  panel.cancelTransmit();
  REQUIRE_FALSE(panel.txScheduled());
  REQUIRE_FALSE(panel.fireTransmit(30'000));  // 取消后到点也不许发
  REQUIRE(fires == 0);

  // 出口拒绝（门禁/无设备）：这一发就废了，不留在等待态反复撞门禁
  panel.setTxController([&](std::vector<int16_t>, uint32_t) {
    ++fires;
    return false;
  });
  REQUIRE(panel.scheduleTransmit("CQ BG5GXO PM01", 1000.0f, -1, 10'000));
  REQUIRE_FALSE(panel.fireTransmit(15'000));
  REQUIRE(fires == 1);
  REQUIRE_FALSE(panel.txScheduled());
}

TEST_CASE("ft8 panel: a late fire is reported as misaligned", "[ft8][ui]") {
  uiApp();
  radio::app::Ft8Panel panel;
  panel.setTxController([](std::vector<int16_t>, uint32_t) { return true; });

  QString msg;
  bool flagged = false;
  QObject::connect(&panel, &radio::app::Ft8Panel::statusMessage,
                   [&](const QString& text, bool err) {
                     msg = text;
                     flagged = err;
                   });

  REQUIRE(panel.scheduleTransmit("CQ BG5GXO PM01", 1000.0f, -1, 10'000));
  REQUIRE(panel.fireTransmit(15'000));  // 准时：只是常规提示
  REQUIRE_FALSE(flagged);
  REQUIRE(panel.scheduleTransmit("CQ BG5GXO PM01", 1000.0f, -1, 46'000));
  REQUIRE(panel.fireTransmit(60'000 + 900));  // GUI 线程被堵：晚了 900 ms
  REQUIRE(flagged);
  REQUIRE(msg.contains("900"));
}
