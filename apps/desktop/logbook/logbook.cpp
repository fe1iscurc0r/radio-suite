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

bool HamLogDb::open() {
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
  if (!const_cast<HamLogDb*>(this)->open()) return {};
  QSqlQuery q(QSqlDatabase::database("radio_logbook"));
  q.exec("SELECT value FROM settings WHERE key='my_callsign'");
  return q.next() ? q.value(0).toString() : QString();
}

std::vector<Qso> HamLogDb::recent(int limit) {
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

std::vector<Qso> HamLogDb::search(const QString& query, int limit) {
  std::vector<Qso> out;
  if (!open()) return out;
  QSqlQuery q(QSqlDatabase::database("radio_logbook"));
  if (query.trimmed().isEmpty()) {
    q.prepare("SELECT id,Callsign,Freq,Year,Month,Day,Time,Mode,Rst_self,"
              "Rst_side,QTH,Device,QSL_RX,QSL_SEND,Remarks,CreateTime "
              "FROM log ORDER BY id DESC LIMIT ?");
    q.addBindValue(limit);
  } else {
    q.prepare("SELECT id,Callsign,Freq,Year,Month,Day,Time,Mode,Rst_self,"
              "Rst_side,QTH,Device,QSL_RX,QSL_SEND,Remarks,CreateTime "
              "FROM log WHERE Callsign LIKE ? OR CAST(Freq AS TEXT) LIKE ? "
              "ORDER BY id DESC LIMIT ?");
    q.addBindValue(query.trimmed() + "%");
    q.addBindValue(query.trimmed() + "%");
    q.addBindValue(limit);
  }
  if (!q.exec()) {
    err_ = "搜索失败: " + q.lastError().text();
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
