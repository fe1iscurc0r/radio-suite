// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "logbook/logbook.h"
#include "qt_test_app.h"

namespace {

QString makeTemporaryDatabase(const QTemporaryDir& dir) {
  const QString path = dir.filePath("Log.db");
  {
    auto db = QSqlDatabase::addDatabase("QSQLITE", "logbook_test_schema");
    db.setDatabaseName(path);
    REQUIRE(db.open());
    QSqlQuery q(db);
    REQUIRE(q.exec("CREATE TABLE log ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "Callsign TEXT,Freq REAL,Year INTEGER,Month INTEGER,Day INTEGER,"
                   "Time TEXT,Mode TEXT,Power_self TEXT,Power_side TEXT,"
                   "Rst_self TEXT,Rst_side TEXT,QTH TEXT,Device TEXT,"
                   "QSL_RX TEXT,QSL_SEND TEXT,Remarks TEXT,CreateTime TEXT)"));
    REQUIRE(q.exec("CREATE TABLE settings (key TEXT PRIMARY KEY,value TEXT)"));
    db.close();
  }
  QSqlDatabase::removeDatabase("logbook_test_schema");
  return path;
}

radio::logbook::Qso sampleQso() {
  radio::logbook::Qso q;
  q.callsign = "BG1TEST";
  q.freq_mhz = 14.270;
  q.year = 2026;
  q.month = 10;
  q.day = 26;
  q.time_hhmm = "1234";
  q.mode = "USB";
  q.qth = QString::fromUtf8("北京");
  q.remarks = QString::fromUtf8("周末通联");
  return q;
}

}  // namespace

TEST_CASE("logbook CRUD and search use a real temporary SQLite database", "[logbook][sqlite]") {
  qtTestApp();
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  radio::logbook::HamLogDb db(makeTemporaryDatabase(dir));
  REQUIRE(db.available());

  REQUIRE(db.insert(sampleQso()));
  auto found = db.search("BG1TEST");
  REQUIRE(found.size() == 1);
  CHECK(found[0].rst_sent == "59");
  CHECK(found[0].rst_rcvd == "59");
  CHECK(found[0].device == "IC-705");
  CHECK(db.search(QString::fromUtf8("北京")).size() == 1);
  CHECK(db.search(QString::fromUtf8("周末")).size() == 1);
  CHECK(db.search("IC-705").size() == 1);
  CHECK(db.recent().size() == 1);

  auto qso = db.getById(found[0].id);
  REQUIRE(qso.has_value());
  qso->callsign = "BG1UPDATED";
  qso->remarks = "updated";
  REQUIRE(db.update(*qso));
  CHECK(db.getById(qso->id)->callsign == "BG1UPDATED");

  REQUIRE(db.setSetting("my_callsign", "BG1HOME"));
  CHECK(db.getSetting("my_callsign") == "BG1HOME");
  CHECK(db.myCallsign() == "BG1HOME");

  REQUIRE(db.remove(qso->id));
  CHECK_FALSE(db.getById(qso->id).has_value());
  CHECK(db.search("").empty());
  QSqlDatabase::removeDatabase("radio_logbook");
}

TEST_CASE("logbook ADIF field lengths count UTF-8 bytes", "[logbook][adif]") {
  const auto adif = radio::logbook::toAdif({sampleQso()}, "BG1HOME");
  CHECK(adif.contains("<CALL:7>BG1TEST"));
  CHECK(adif.contains(QString::fromUtf8("<QTH:6>北京")));
  CHECK(adif.contains("<STATION_CALLSIGN:7>BG1HOME"));
  CHECK(adif.contains("<EOR>"));
}
