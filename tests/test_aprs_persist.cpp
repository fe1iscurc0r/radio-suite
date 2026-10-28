// SPDX-License-Identifier: AGPL-3.0-or-later
// 持久化测试：临时目录建库（绝不触碰 HamLog Log.db）
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>
#include <QString>

#include "aprs/aprs.h"
#include "aprs/persist.h"
#include "aprs/store.h"
#include "qt_test_app.h"

namespace {
radio::aprs::Packet parse(const char* line) {
  radio::aprs::Packet p;
  std::string err;
  REQUIRE(radio::aprs::parseTnc2(line, &p, &err));
  return p;
}
}  // namespace

TEST_CASE("aprs: sqlite snapshot roundtrip", "[aprs][db]") {
  qtTestApp();
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath("aprs_test.db");

  radio::aprs::StationStore stations(100, 8);
  radio::aprs::MessageStore messages;

  const auto p1 = parse("N0CALL-9>APRS,WIDE1-1:!4903.50N/07201.75W-Test");
  stations.ingest(p1, 1000);
  const auto p2 = parse(
      "N0CALL-9>APRS,WIDE1-1:@092345z4903.60N/07201.80W>/A=000100");
  stations.ingest(p2, 20000);
  const auto obj = parse("N0CALL>APRS:;LEADER   *092345z4903.50N/07201.75W>");
  stations.ingest(obj, 21000);

  const auto inbound = parse("W1AW>APRS::N0CALL   :hello there{12");
  REQUIRE(messages.ingest(inbound, "N0CALL", 30000));
  const auto id = messages.enqueue("W1AW", "reply text", 31000);
  messages.noteTry(id, 32000);
  messages.acknowledge(id, false);

  radio::aprs::AprsDb db(path);
  QString err;
  const bool opened = db.open(&err);
  INFO(err.toStdString());
  REQUIRE(opened);
  REQUIRE(db.save(stations, messages, &err));
  db.close();

  radio::aprs::StationStore restored(100, 8);
  radio::aprs::MessageStore restored_msgs;
  radio::aprs::AprsDb db2(path);
  REQUIRE(db2.open(&err));
  REQUIRE(db2.load(&restored, &restored_msgs, &err));

  CHECK(restored.size() == stations.size());
  const auto* n0 = restored.find("N0CALL-9");
  REQUIRE(n0 != nullptr);
  CHECK(n0->packets == 2);
  CHECK(n0->has_pos);
  CHECK((n0->pos.lat > 49.0 && n0->pos.lat < 49.2));
  CHECK(n0->track.size() == 2);
  CHECK(n0->comment == "Test");
  CHECK(n0->altitude_ft > 90.0);

  const auto* leader = restored.find("LEADER");
  REQUIRE(leader != nullptr);
  CHECK(leader->is_object);

  REQUIRE(restored_msgs.inbox().size() == 1);
  CHECK(restored_msgs.inbox()[0].from == "W1AW");
  CHECK(restored_msgs.inbox()[0].text == "hello there");
  CHECK(restored_msgs.inbox()[0].id == "12");
  CHECK(restored_msgs.unread() == 1);

  REQUIRE(restored_msgs.outbox().size() == 1);
  CHECK(restored_msgs.outbox()[0].id == id);
  CHECK(restored_msgs.outbox()[0].acked);
  CHECK(restored_msgs.outbox()[0].tries == 1);

  const auto next = restored_msgs.enqueue("W1AW", "second", 40000);
  CHECK(next != id);
  db2.close();
}
