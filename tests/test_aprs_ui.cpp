// SPDX-License-Identifier: AGPL-3.0-or-later
// APRS 面板离屏 UI 测试：构造/位置/地图绘制/消息表（临时库，禁瓦片网络）
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include "app/aprs_map.h"
#include "app/aprs_messages_dialog.h"
#include "app/aprs_panel.h"
#include "aprs/aprs.h"
#include "aprs/store.h"

namespace {
QApplication& uiApp() {
  static int argc = 1;
  static char name[] = "aprs-ui-tests";
  static char* argv[] = {name, nullptr};
  static QApplication app(argc, argv);
  return app;
}
}  // namespace

TEST_CASE("aprs panel builds offscreen with temp db", "[aprs][ui]") {
  uiApp();
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  qputenv("RADIO_SUITE_APRS_DB",
          dir.filePath("aprs_ui.db").toLocal8Bit());
  qputenv("RADIO_SUITE_APRS_TILES", "off");

  radio::app::AprsPanel panel;
  panel.setLoginCall("N0CALL");
  panel.setMyPosition(true, 31.2304, 121.4737);
  panel.resize(1000, 700);
  panel.show();
  QApplication::processEvents();
  CHECK(panel.loginCall() == "N0CALL");
  CHECK_FALSE(panel.isConnected());
  CHECK_FALSE(panel.rfConnected());
  CHECK(panel.unread() == 0);
  panel.hide();
}

TEST_CASE("aprs map paints stations without tiles", "[aprs][ui]") {
  uiApp();
  qputenv("RADIO_SUITE_APRS_TILES", "off");
  radio::aprs::StationStore store(100, 8);
  radio::aprs::Packet p;
  std::string err;
  REQUIRE(radio::aprs::parseTnc2("N0CALL-9>APRS:!4903.50N/07201.75W-Test",
                                 &p, &err));
  store.ingest(p, 1000);
  REQUIRE(radio::aprs::parseTnc2("N0CALL>APRS:;LEADER   *092345z4903.50N/07201.75W>",
                                 &p, &err));
  store.ingest(p, 2000);

  radio::app::AprsMapWidget map;
  map.setStations(&store);
  map.setCenter(49.06, -72.03, 9);
  map.setMyPosition(true, 49.0, -72.0);
  map.resize(640, 400);
  map.show();
  QApplication::processEvents();
  const QPixmap shot = map.grab();
  CHECK_FALSE(shot.isNull());
  CHECK(shot.width() >= 640);
  map.hide();
}

TEST_CASE("aprs messages dialog reloads inbox/outbox", "[aprs][ui]") {
  uiApp();
  radio::aprs::MessageStore store;
  radio::aprs::Packet p;
  std::string err;
  REQUIRE(radio::aprs::parseTnc2("W1AW>APRS::N0CALL   :hello{42", &p, &err));
  REQUIRE(store.ingest(p, "N0CALL", 30000));
  store.enqueue("W1AW", "reply", 31000);

  radio::app::AprsMessagesDialog dlg;
  dlg.setMyCall("N0CALL");
  dlg.reload(store);
  dlg.show();
  QApplication::processEvents();
  CHECK(store.unread() == 1);
  dlg.hide();
}
