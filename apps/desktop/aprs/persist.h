// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · aprs/persist.h — APRS 台站/消息快照持久化（独立 SQLite）
//
// 不复用 HamLog 的 Log.db：APRS 数据是易失观测记录，与 QSO 日志生命周期不同。
// 默认库在 %APPDATA%/radio-suite/aprs.db；保存为整表快照（事务内 clear+insert），
// 台站 ≤ 1000、消息 ≤ 500，写频率由调用方控制（启动载入、退出/定时保存）。
#pragma once

#include <QString>

namespace radio::aprs {

class StationStore;
class MessageStore;

class AprsDb {
 public:
  explicit AprsDb(QString path = {});
  ~AprsDb();

  static QString defaultPath();  // %APPDATA%/radio-suite/aprs.db

  bool open(QString* error = nullptr);
  void close();
  bool isOpen() const;
  QString path() const { return path_; }

  bool save(const StationStore& stations, const MessageStore& messages,
            QString* error = nullptr);
  bool load(StationStore* stations, MessageStore* messages,
            QString* error = nullptr);

 private:
  bool ensureSchema(QString* error);
  QString path_;
  QString conn_;
};

}  // namespace radio::aprs
