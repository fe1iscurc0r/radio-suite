// SPDX-License-Identifier: AGPL-3.0-or-later
#include "logbook/logbook.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace radio::logbook {

// HamLog log 表 18 列（与 hamlog_adapter._LOG_TABLE_COLUMNS 对齐）
namespace {
constexpr const char* kCols[] = {
    "id", "Callsign", "Freq", "Year", "Month", "Day", "Time", "Mode",
    "Power_self", "Power_side", "Rst_self", "Rst_side", "QTH", "Device",
    "QSL_RX", "QSL_SEND", "Remarks", "CreateTime"};
}  // namespace

QString HamLogDb::defaultDbPath() {
  const QString appdata = qEnvironmentVariable("APPDATA");
  if (appdata.isEmpty()) return {};
  return QDir(appdata).filePath("HamLog/Log.db");
}

HamLogDb::HamLogDb(QString db_path)
    : path_(db_path.isEmpty() ? HamLogDb::defaultDbPath() : db_path) {}

bool HamLogDb::available() const {
  if (path_.isEmpty() || !QFile::exists(path_)) {
    err_ = "HamLog 库不存在: " + path_ + "（先运行一次 HamLog）";
    return false;
  }
  return true;
}

bool HamLogDb::open() const {
  if (!available()) return false;
  // 单连接复用：同名连接即已打开
  const QString conn = "radio_logbook";
  QSqlDatabase db = QSqlDatabase::contains(conn)
                        ? QSqlDatabase::database(conn)
                        : QSqlDatabase::addDatabase("QSQLITE", conn);
  db.setDatabaseName(path_);
  if (!db.open()) {
    err_ = "打开失败: " + db.lastError().text();
    return false;
  }
  return true;
}

QString HamLogDb::myCallsign() const {
  if (!open()) return {};
  QSqlQuery q(QSqlDatabase::database("radio_logbook"));
  q.exec("SELECT value FROM settings WHERE key='my_callsign'");
  return q.next() ? q.value(0).toString() : QString();
}

std::vector<Qso> HamLogDb::recent(int limit) const {
  std::vector<Qso> out;
  if (!open()) return out;
  QSqlQuery q(QSqlDatabase::database("radio_logbook"));
  q.prepare("SELECT id,Callsign,Freq,Year,Month,Day,Time,Mode,Rst_self,Rst_side,"
            "QTH,Device,QSL_RX,QSL_SEND,Remarks,CreateTime "
            "FROM log ORDER BY id DESC LIMIT ?");
  q.addBindValue(limit);
  if (!q.exec()) {
    err_ = "查询失败: " + q.lastError().text();
    return out;
  }
  while (q.next()) {
    Qso r;
    r.id = q.value(0).toLongLong();
    r.callsign = q.value(1).toString();
    r.freq_mhz = q.value(2).toDouble();
    r.year = q.value(3).toInt();
    r.month = q.value(4).toInt();
    r.day = q.value(5).toInt();
    r.time_hhmm = q.value(6).toString();
    r.mode = q.value(7).toString();
    r.rst_sent = q.value(8).toString();
    r.rst_rcvd = q.value(9).toString();
    r.qth = q.value(10).toString();
    r.device = q.value(11).toString();
    r.qsl_rx = q.value(12).toString();
    r.qsl_send = q.value(13).toString();
    r.remarks = q.value(14).toString();
    r.create_time = q.value(15).toString();
    out.push_back(std::move(r));
  }
  return out;
}

namespace {
// SELECT 列序（多处共用）：16 列
constexpr const char* kSelectCols =
    "SELECT id,Callsign,Freq,Year,Month,Day,Time,Mode,Rst_self,"
    "Rst_side,QTH,Device,QSL_RX,QSL_SEND,Remarks,CreateTime ";
Qso rowToQso(const QSqlQuery& q) {
  Qso r;
  r.id = q.value(0).toLongLong();
  r.callsign = q.value(1).toString();
  r.freq_mhz = q.value(2).toDouble();
  r.year = q.value(3).toInt();
  r.month = q.value(4).toInt();
  r.day = q.value(5).toInt();
  r.time_hhmm = q.value(6).toString();
  r.mode = q.value(7).toString();
  r.rst_sent = q.value(8).toString();
  r.rst_rcvd = q.value(9).toString();
  r.qth = q.value(10).toString();
  r.device = q.value(11).toString();
  r.qsl_rx = q.value(12).toString();
  r.qsl_send = q.value(13).toString();
  r.remarks = q.value(14).toString();
  r.create_time = q.value(15).toString();
  return r;
}
}  // namespace

std::vector<Qso> HamLogDb::search(const QString& keyword, int limit) const {
  std::vector<Qso> out;
  if (!open()) return out;
  QSqlQuery q(QSqlDatabase::database("radio_logbook"));
  if (keyword.trimmed().isEmpty()) {
    q.prepare(QString(kSelectCols) +
              "FROM log ORDER BY Year DESC, Month DESC, Day DESC, Time DESC LIMIT ?");
    q.addBindValue(limit);
  } else {
    // 原版语义（AutoDeal.search）：Callsign/QTH/Device/Remarks 四字段 %kw% 模糊
    const QString pattern = "%" + keyword.trimmed() + "%";
    q.prepare(QString(kSelectCols) +
              "FROM log WHERE Callsign LIKE ? OR QTH LIKE ? OR Device LIKE ? "
              "OR Remarks LIKE ? "
              "ORDER BY Year DESC, Month DESC, Day DESC, Time DESC LIMIT ?");
    q.addBindValue(pattern);
    q.addBindValue(pattern);
    q.addBindValue(pattern);
    q.addBindValue(pattern);
    q.addBindValue(limit);
  }
  if (!q.exec()) {
    err_ = "搜索失败: " + q.lastError().text();
    return out;
  }
  while (q.next()) out.push_back(rowToQso(q));
  return out;
}

std::optional<Qso> HamLogDb::getById(int64_t id) const {
  if (!open()) return std::nullopt;
  QSqlQuery q(QSqlDatabase::database("radio_logbook"));
  q.prepare(QString(kSelectCols) + "FROM log WHERE id=?");
  q.addBindValue(static_cast<qlonglong>(id));
  if (!q.exec() || !q.next()) return std::nullopt;
  return rowToQso(q);
}

bool HamLogDb::update(const Qso& q) {
  if (!open() || q.id == 0) return false;
  QSqlQuery u(QSqlDatabase::database("radio_logbook"));
  u.prepare("UPDATE log SET Callsign=?,Freq=?,Year=?,Month=?,Day=?,Time=?,"
            "Mode=?,Rst_self=?,Rst_side=?,QTH=?,Device=?,QSL_RX=?,QSL_SEND=?,"
            "Remarks=? WHERE id=?");
  u.addBindValue(q.callsign);
  u.addBindValue(q.freq_mhz);
  u.addBindValue(q.year);
  u.addBindValue(q.month);
  u.addBindValue(q.day);
  u.addBindValue(q.time_hhmm);
  u.addBindValue(q.mode);
  u.addBindValue(q.rst_sent);
  u.addBindValue(q.rst_rcvd);
  u.addBindValue(q.qth);
  u.addBindValue(q.device);
  u.addBindValue(q.qsl_rx);
  u.addBindValue(q.qsl_send);
  u.addBindValue(q.remarks);
  u.addBindValue(static_cast<qlonglong>(q.id));
  if (!u.exec()) {
    err_ = "更新失败: " + u.lastError().text();
    return false;
  }
  return true;
}

bool HamLogDb::remove(int64_t id) {
  if (!open()) return false;
  QSqlQuery q(QSqlDatabase::database("radio_logbook"));
  q.prepare("DELETE FROM log WHERE id=?");
  q.addBindValue(static_cast<qlonglong>(id));
  if (!q.exec()) {
    err_ = "删除失败: " + q.lastError().text();
    return false;
  }
  return true;
}

QString HamLogDb::getSetting(const QString& key) const {
  if (!open()) return {};
  QSqlQuery q(QSqlDatabase::database("radio_logbook"));
  q.prepare("SELECT value FROM settings WHERE key=?");
  q.addBindValue(key);
  return q.exec() && q.next() ? q.value(0).toString() : QString();
}

bool HamLogDb::setSetting(const QString& key, const QString& value) {
  if (!open()) return false;
  QSqlQuery q(QSqlDatabase::database("radio_logbook"));
  q.prepare("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)");
  q.addBindValue(key);
  q.addBindValue(value);
  if (!q.exec()) {
    err_ = "设置写入失败: " + q.lastError().text();
    return false;
  }
  return true;
}

bool HamLogDb::insert(const Qso& q) {
  if (!open()) return false;
  QSqlQuery ins(QSqlDatabase::database("radio_logbook"));
  ins.prepare("INSERT INTO log (Callsign,Freq,Year,Month,Day,Time,Mode,"
              "Rst_self,Rst_side,QTH,Device,QSL_RX,QSL_SEND,Remarks,CreateTime) "
              "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  ins.addBindValue(q.callsign);
  ins.addBindValue(q.freq_mhz);
  ins.addBindValue(q.year);
  ins.addBindValue(q.month);
  ins.addBindValue(q.day);
  ins.addBindValue(q.time_hhmm);
  ins.addBindValue(q.mode);
  ins.addBindValue(q.rst_sent.isEmpty() ? "59" : q.rst_sent);
  ins.addBindValue(q.rst_rcvd.isEmpty() ? "59" : q.rst_rcvd);
  ins.addBindValue(q.qth);
  ins.addBindValue(q.device.isEmpty() ? "IC-705" : q.device);
  ins.addBindValue(q.qsl_rx);
  ins.addBindValue(q.qsl_send);
  ins.addBindValue(q.remarks);
  ins.addBindValue(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd hh:mm:ss"));
  if (!ins.exec()) {
    err_ = "写入失败: " + ins.lastError().text();
    return false;
  }
  return true;
}

// ---- ADIF（设计移植自 scratchpad tools/adif.py，AGPL-3.0；格式为 ADIF 开放标准）----
namespace {
void adifField(QString* out, const char* name, const QString& value) {
  if (value.isEmpty()) return;
  out->append(QString("<%1:%2>%3").arg(name).arg(value.size()).arg(value));
}
}  // namespace

QString toAdif(const std::vector<Qso>& qsos, const QString& my_call) {
  QString out;
  out += "<ADIF_VER:5>3.1.4<PROGRAMID:11>radio-suite<EOH>\n";
  for (const Qso& q : qsos) {
    adifField(&out, "CALL", q.callsign);
    adifField(&out, "STATION_CALLSIGN", my_call);
    if (q.freq_mhz > 0)
      adifField(&out, "FREQ", QString::number(q.freq_mhz, 'f', 6));
    adifField(&out, "QSO_DATE", QString("%1%2%3")
                                  .arg(q.year, 4, 10, QChar('0'))
                                  .arg(q.month, 2, 10, QChar('0'))
                                  .arg(q.day, 2, 10, QChar('0')));
    adifField(&out, "TIME_ON", q.time_hhmm + "00");
    adifField(&out, "MODE", q.mode);
    adifField(&out, "RST_SENT", q.rst_sent);
    adifField(&out, "RST_RCVD", q.rst_rcvd);
    adifField(&out, "QTH", q.qth);
    adifField(&out, "NOTES", q.remarks);
    out += "<EOR>\n";
  }
  return out;
}

}  // namespace radio::logbook
