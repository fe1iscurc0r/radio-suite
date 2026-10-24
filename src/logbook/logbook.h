// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · logbook/logbook.h — HamLog 融合：直读直写 HamLog(R) 的 Log.db
//
// 数据路径与字段与 scratchpad hamlog_adapter（AGPL，D:/…/scratchpad-unified/
// mcpserver/adapters/hamlog_adapter/adapter.py）完全一致——同一份日志库，
// 两个程序天然互通。HamLog R1.0.0 为 PyQt6 桌面程序，库在
// %APPDATA%/HamLog/Log.db，log 表 18 列（见 .cpp）。
#pragma once

#include <cstdint>
#include <optional>
#include <QString>
#include <QStringList>
#include <vector>

namespace radio::logbook {

struct Qso {
  int64_t id = 0;         // 0 = 新录入
  QString callsign;
  double freq_mhz = 0.0;
  int year = 0, month = 0, day = 0;
  QString time_hhmm;      // HamLog Time 字段（HHMM）
  QString mode;
  QString rst_sent, rst_rcvd;
  QString qth;
  QString device;
  QString remarks;
  QString qsl_rx, qsl_send;  // YYYYMMDD，空=未收/未发
  QString create_time;
};

class HamLogDb {
 public:
  // %APPDATA%/HamLog/Log.db（Windows）；不存在返回空路径
  static QString defaultDbPath();

  explicit HamLogDb(QString db_path = {});

  bool available() const;          // 库文件存在且 log 表可读
  const QString& lastError() const { return err_; }
  QString myCallsign() const;      // settings 表 my_callsign（无则空）

  std::vector<Qso> recent(int limit = 8);
  bool insert(const Qso& q);       // 写入 HamLog 库（radio-suite 与 HamLog 立即可见）

 private:
  bool open();
  QString path_;
  mutable QString err_;  // const 探测方法（available）也要能记错误
};

// ADIF 3.x 导出（子集）。设计移植自 scratchpad tools/adif.py（AGPL-3.0，
// 修改：C++ 重写、字段按 radio-suite 需要裁剪），字段格式为 ADIF 开放标准。
QString toAdif(const std::vector<Qso>& qsos, const QString& my_call);

}  // namespace radio::logbook
