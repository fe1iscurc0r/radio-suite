// SPDX-License-Identifier: AGPL-3.0-or-later
#include "aprs/persist.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include <atomic>

#include "aprs/store.h"

namespace radio::aprs {
namespace {

std::atomic<int> g_conn_seq{0};

QString encodeWeather(const Weather& w) {
  if (!w.present) return {};
  auto num = [](bool has, double v) {
    return has ? QString::number(v, 'g', 12) : QStringLiteral("n");
  };
  return QStringList{num(w.has_wind, w.wind_dir_deg),
                     num(w.has_wind, w.wind_kt), num(w.has_wind, w.gust_kt),
                     num(w.has_temp, w.temp_c),
                     num(w.has_rain_1h, w.rain_1h_mm),
                     num(w.has_rain_24h, w.rain_24h_mm),
                     num(w.has_humidity, w.humidity),
                     num(w.has_pressure, w.pressure_hpa)}
      .join(';');
}

Weather decodeWeather(const QString& s) {
  Weather w;
  if (s.isEmpty()) return w;
  const auto parts = s.split(';');
  if (parts.size() < 8) return w;
  auto num = [&](int i, bool* has, double* v) {
    if (parts[i] == "n") return;
    bool ok = false;
    const double d = parts[i].toDouble(&ok);
    if (ok) {
      *has = true;
      *v = d;
    }
  };
  num(0, &w.has_wind, &w.wind_dir_deg);
  num(1, &w.has_wind, &w.wind_kt);
  num(2, &w.has_wind, &w.gust_kt);
  num(3, &w.has_temp, &w.temp_c);
  num(4, &w.has_rain_1h, &w.rain_1h_mm);
  num(5, &w.has_rain_24h, &w.rain_24h_mm);
  if (parts[6] != "n") {
    bool ok = false;
    const int h = parts[6].toInt(&ok);
    if (ok) {
      w.has_humidity = true;
      w.humidity = h;
    }
  }
  num(7, &w.has_pressure, &w.pressure_hpa);
  w.present = true;
  return w;
}

QString encodeTelemetry(const Telemetry& t) {
  if (!t.present) return {};
  QStringList parts{QString::number(t.seq)};
  for (double a : t.analog) parts << QString::number(a, 'g', 12);
  parts << QString::number(static_cast<int>(t.digital));
  return parts.join(';');
}

Telemetry decodeTelemetry(const QString& s) {
  Telemetry t;
  if (s.isEmpty()) return t;
  const auto parts = s.split(';');
  if (parts.size() != 7) return t;
  t.seq = parts[0].toInt();
  for (int i = 0; i < 5; ++i) t.analog[i] = parts[i + 1].toDouble();
  t.digital = static_cast<uint8_t>(parts[6].toInt());
  t.present = true;
  return t;
}

}  // namespace

AprsDb::AprsDb(QString path) : path_(std::move(path)) {
  if (path_.isEmpty()) path_ = defaultPath();
}

AprsDb::~AprsDb() { close(); }

QString AprsDb::defaultPath() {
  // 测试/便携模式可用环境变量改库位置，避免碰用户数据
  const QByteArray override = qgetenv("RADIO_SUITE_APRS_DB");
  if (!override.isEmpty()) return QString::fromLocal8Bit(override);
  const QString appdata = qEnvironmentVariable("APPDATA");
  const QString base = appdata.isEmpty()
                           ? QStandardPaths::writableLocation(
                                 QStandardPaths::AppDataLocation)
                           : QDir(appdata).filePath("radio-suite");
  return QDir(base).filePath("aprs.db");
}

bool AprsDb::isOpen() const {
  return !conn_.isEmpty() && QSqlDatabase::database(conn_, false).isOpen();
}

bool AprsDb::open(QString* error) {
  if (isOpen()) return true;
  const QFileInfo fi(path_);
  if (!fi.absoluteDir().exists() && !QDir().mkpath(fi.absolutePath())) {
    if (error) *error = QString("无法创建目录 %1").arg(fi.absolutePath());
    return false;
  }
  if (QSqlDatabase::isDriverAvailable("QSQLITE") == false) {
    if (error) *error = "Qt SQLite 驱动不可用";
    return false;
  }
  conn_ = QString("aprs-%1").arg(++g_conn_seq);
  auto db = QSqlDatabase::addDatabase("QSQLITE", conn_);
  db.setDatabaseName(path_);
  if (!db.open()) {
    if (error) *error = db.lastError().text();
    QSqlDatabase::removeDatabase(conn_);
    conn_.clear();
    return false;
  }
  if (!ensureSchema(error)) {
    close();
    return false;
  }
  return true;
}

void AprsDb::close() {
  if (conn_.isEmpty()) return;
  {
    auto db = QSqlDatabase::database(conn_, false);
    if (db.isValid() && db.isOpen()) db.close();
  }
  QSqlDatabase::removeDatabase(conn_);
  conn_.clear();
}

bool AprsDb::ensureSchema(QString* error) {
  auto db = QSqlDatabase::database(conn_, false);
  QSqlQuery q(db);
  const QStringList ddl = {
      "CREATE TABLE IF NOT EXISTS stations("
      "key TEXT PRIMARY KEY, call TEXT, name TEXT, is_object INTEGER, alive INTEGER,"
      "lat REAL, lon REAL, has_pos INTEGER, sym_table TEXT, sym_code TEXT,"
      "comment TEXT, status TEXT, via TEXT, last_info TEXT,"
      "packets INTEGER, first_ms INTEGER, last_ms INTEGER,"
      "course REAL, speed REAL, altitude REAL, weather TEXT, telemetry TEXT,"
      "messaging INTEGER)",
      "CREATE TABLE IF NOT EXISTS track("
      "station TEXT, at_ms INTEGER, lat REAL, lon REAL,"
      "PRIMARY KEY(station, at_ms))",
      "CREATE TABLE IF NOT EXISTS inbox("
      "from_call TEXT, to_call TEXT, body TEXT, msg_id TEXT, line INTEGER,"
      "at_ms INTEGER, read INTEGER, PRIMARY KEY(from_call, at_ms, msg_id))",
      "CREATE TABLE IF NOT EXISTS outbox("
      "to_call TEXT, body TEXT, msg_id TEXT PRIMARY KEY, created_ms INTEGER,"
      "last_try_ms INTEGER, tries INTEGER, acked INTEGER, rejected INTEGER,"
      "retry INTEGER)"};
  for (const auto& sql : ddl) {
    if (!q.exec(sql)) {
      if (error) *error = q.lastError().text();
      return false;
    }
  }
  return true;
}

bool AprsDb::save(const StationStore& stations, const MessageStore& messages,
                  QString* error) {
  if (!isOpen() && !open(error)) return false;
  auto db = QSqlDatabase::database(conn_, false);
  if (!db.transaction()) {
    if (error) *error = db.lastError().text();
    return false;
  }
  auto fail = [&](const QSqlQuery& q) {
    if (error) *error = q.lastError().text();
    db.rollback();
    return false;
  };
  QSqlQuery del(db);
  for (const char* table : {"stations", "track", "inbox", "outbox"})
    if (!del.exec(QString("DELETE FROM %1").arg(table))) return fail(del);

  QSqlQuery ins(db);
  ins.prepare(
      "INSERT INTO stations VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  for (const auto& [key, st] : stations.all()) {
    ins.addBindValue(QString::fromStdString(st.key));
    ins.addBindValue(QString::fromStdString(st.call));
    ins.addBindValue(QString::fromStdString(st.name));
    ins.addBindValue(st.is_object ? 1 : 0);
    ins.addBindValue(st.alive ? 1 : 0);
    ins.addBindValue(st.pos.lat);
    ins.addBindValue(st.pos.lon);
    ins.addBindValue(st.has_pos ? 1 : 0);
    ins.addBindValue(QString(QChar(st.symbol.table)));
    ins.addBindValue(QString(QChar(st.symbol.code)));
    ins.addBindValue(QString::fromStdString(st.comment));
    ins.addBindValue(QString::fromStdString(st.status));
    ins.addBindValue(QString::fromStdString(st.via));
    ins.addBindValue(QString::fromStdString(st.last_info));
    ins.addBindValue(st.packets);
    ins.addBindValue(static_cast<qint64>(st.first_ms));
    ins.addBindValue(static_cast<qint64>(st.last_ms));
    ins.addBindValue(st.course_deg);
    ins.addBindValue(st.speed_kt);
    ins.addBindValue(st.altitude_ft);
    ins.addBindValue(encodeWeather(st.weather));
    ins.addBindValue(encodeTelemetry(st.telemetry));
    ins.addBindValue(st.messaging ? 1 : 0);
    if (!ins.exec()) return fail(ins);
  }

  QSqlQuery trk(db);
  trk.prepare("INSERT INTO track VALUES(?,?,?,?)");
  for (const auto& [key, st] : stations.all()) {
    for (const auto& tp : st.track) {
      trk.addBindValue(QString::fromStdString(st.key));
      trk.addBindValue(static_cast<qint64>(tp.at_ms));
      trk.addBindValue(tp.pos.lat);
      trk.addBindValue(tp.pos.lon);
      if (!trk.exec()) return fail(trk);
    }
  }

  QSqlQuery in(db);
  in.prepare("INSERT INTO inbox VALUES(?,?,?,?,?,?,?)");
  for (const auto& m : messages.inbox()) {
    in.addBindValue(QString::fromStdString(m.from));
    in.addBindValue(QString::fromStdString(m.to));
    in.addBindValue(QString::fromStdString(m.text));
    in.addBindValue(QString::fromStdString(m.id));
    in.addBindValue(m.line);
    in.addBindValue(static_cast<qint64>(m.at_ms));
    in.addBindValue(m.read ? 1 : 0);
    if (!in.exec()) return fail(in);
  }

  QSqlQuery out(db);
  out.prepare("INSERT INTO outbox VALUES(?,?,?,?,?,?,?,?,?)");
  for (const auto& m : messages.outbox()) {
    out.addBindValue(QString::fromStdString(m.to));
    out.addBindValue(QString::fromStdString(m.text));
    out.addBindValue(QString::fromStdString(m.id));
    out.addBindValue(static_cast<qint64>(m.created_ms));
    out.addBindValue(static_cast<qint64>(m.last_try_ms));
    out.addBindValue(m.tries);
    out.addBindValue(m.acked ? 1 : 0);
    out.addBindValue(m.rejected ? 1 : 0);
    out.addBindValue(m.retry_enabled ? 1 : 0);
    if (!out.exec()) return fail(out);
  }

  if (!db.commit()) {
    if (error) *error = db.lastError().text();
    return false;
  }
  return true;
}

bool AprsDb::load(StationStore* stations, MessageStore* messages,
                  QString* error) {
  if (!isOpen() && !open(error)) return false;
  auto db = QSqlDatabase::database(conn_, false);

  std::vector<Station> loaded;
  QSqlQuery q(db);
  if (!q.exec("SELECT key, call, name, is_object, alive, lat, lon, has_pos,"
              "sym_table, sym_code, comment, status, via, last_info, packets,"
              "first_ms, last_ms, course, speed, altitude, weather, telemetry,"
              "messaging FROM stations")) {
    if (error) *error = q.lastError().text();
    return false;
  }
  while (q.next()) {
    Station st;
    st.key = q.value(0).toString().toStdString();
    st.call = q.value(1).toString().toStdString();
    st.name = q.value(2).toString().toStdString();
    st.is_object = q.value(3).toInt() != 0;
    st.alive = q.value(4).toInt() != 0;
    st.pos.lat = q.value(5).toDouble();
    st.pos.lon = q.value(6).toDouble();
    st.has_pos = q.value(7).toInt() != 0;
    const QString table = q.value(8).toString();
    const QString code = q.value(9).toString();
    st.symbol.table = table.isEmpty() ? '/' : table.at(0).toLatin1();
    st.symbol.code = code.isEmpty() ? '/' : code.at(0).toLatin1();
    st.comment = q.value(10).toString().toStdString();
    st.status = q.value(11).toString().toStdString();
    st.via = q.value(12).toString().toStdString();
    st.last_info = q.value(13).toString().toStdString();
    st.packets = q.value(14).toInt();
    st.first_ms = q.value(15).toLongLong();
    st.last_ms = q.value(16).toLongLong();
    st.course_deg = q.value(17).toDouble();
    st.speed_kt = q.value(18).toDouble();
    st.altitude_ft = q.value(19).toDouble();
    st.weather = decodeWeather(q.value(20).toString());
    st.telemetry = decodeTelemetry(q.value(21).toString());
    st.messaging = q.value(22).toInt() != 0;
    loaded.push_back(std::move(st));
  }

  QSqlQuery tq(db);
  if (!tq.exec("SELECT station, at_ms, lat, lon FROM track ORDER BY at_ms")) {
    if (error) *error = tq.lastError().text();
    return false;
  }
  while (tq.next()) {
    const std::string key = tq.value(0).toString().toStdString();
    for (auto& st : loaded) {
      if (st.key == key) {
        st.track.push_back({LatLon{tq.value(2).toDouble(), tq.value(3).toDouble()},
                            tq.value(1).toLongLong()});
        break;
      }
    }
  }
  if (stations) stations->restore(std::move(loaded));

  std::vector<InboxMessage> inbox;
  QSqlQuery iq(db);
  if (!iq.exec("SELECT from_call, to_call, body, msg_id, line, at_ms, read"
               " FROM inbox ORDER BY at_ms")) {
    if (error) *error = iq.lastError().text();
    return false;
  }
  while (iq.next()) {
    InboxMessage m;
    m.from = iq.value(0).toString().toStdString();
    m.to = iq.value(1).toString().toStdString();
    m.text = iq.value(2).toString().toStdString();
    m.id = iq.value(3).toString().toStdString();
    m.line = iq.value(4).toInt();
    m.at_ms = iq.value(5).toLongLong();
    m.read = iq.value(6).toInt() != 0;
    inbox.push_back(std::move(m));
  }

  std::vector<OutboxMessage> outbox;
  QSqlQuery oq(db);
  if (!oq.exec("SELECT to_call, body, msg_id, created_ms, last_try_ms, tries,"
               "acked, rejected, retry FROM outbox ORDER BY created_ms")) {
    if (error) *error = oq.lastError().text();
    return false;
  }
  while (oq.next()) {
    OutboxMessage m;
    m.to = oq.value(0).toString().toStdString();
    m.text = oq.value(1).toString().toStdString();
    m.id = oq.value(2).toString().toStdString();
    m.created_ms = oq.value(3).toLongLong();
    m.last_try_ms = oq.value(4).toLongLong();
    m.tries = oq.value(5).toInt();
    m.acked = oq.value(6).toInt() != 0;
    m.rejected = oq.value(7).toInt() != 0;
    m.retry_enabled = oq.value(8).toInt() != 0;
    outbox.push_back(std::move(m));
  }
  if (messages) messages->restore(std::move(inbox), std::move(outbox));
  return true;
}

}  // namespace radio::aprs
