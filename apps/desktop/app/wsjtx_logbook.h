// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/wsjtx_logbook.h — WSJT-X QSO Logged → HamLog 记录映射
#pragma once

#include <QDateTime>
#include <QString>

#include "logbook/logbook.h"
#include "wsjtx/wsjtx.h"

namespace radio::app {

inline radio::logbook::Qso qsoFromWsjtx(const radio::wsjtx::QsoLogged& logged) {
  const QDateTime utc =
      QDateTime::fromMSecsSinceEpoch(logged.date_time_off.utcMs()).toUTC();
  radio::logbook::Qso qso;
  qso.callsign = QString::fromStdString(logged.dx_call).trimmed().toUpper();
  qso.freq_mhz = static_cast<double>(logged.tx_frequency) / 1e6;
  qso.year = utc.date().year();
  qso.month = utc.date().month();
  qso.day = utc.date().day();
  qso.time_hhmm = utc.time().toString("hhmm");
  qso.mode = QString::fromStdString(logged.mode).trimmed();
  qso.rst_sent = QString::fromStdString(logged.report_sent);
  qso.rst_rcvd = QString::fromStdString(logged.report_received);
  qso.device = "WSJT-X";
  const QString grid = QString::fromStdString(logged.dx_grid).trimmed();
  if (!grid.isEmpty()) qso.remarks = QString("网格 %1").arg(grid);
  return qso;
}

}  // namespace radio::app
