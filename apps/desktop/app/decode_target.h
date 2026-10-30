// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/decode_target.h — 从 FT8/WSJT-X 解码文本里挑呼号与网格
//
// 文本形态：CQ [修饰…] CALL [GRID] / CALL1 CALL2 [GRID|报告|73]
// 修饰词（DX/TEST/POTA/SOTA/QRP/FD/WW/VHF…）都「不像呼号」，用同一判据筛掉，
// 不再维护一份必然漏项的清单。WSJT-X 面板与本机 FT8 面板共用。
#pragma once

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

namespace radio::app {

struct DecodeTarget {
  QString call;
  QString grid;
};

inline bool plausibleCall(const QString& token) {
  static const QSet<QString> kProtocol{"RR73", "RRR", "RR", "73", "R",
                                       "TU",   "DE",  "CQ", "QRZ"};
  if (kProtocol.contains(token.toUpper())) return false;
  if (token.size() < 3 || token.size() > 12) return false;
  bool letter = false, digit = false;
  for (const QChar c : token) {
    if (!c.isLetterOrNumber() && c != '/') return false;  // 排除 -12 / R-12 报告
    if (c.isLetter()) letter = true;
    if (c.isDigit()) digit = true;
  }
  return letter && digit;
}

inline bool looksLikeGrid(const QString& token) {
  static const QRegularExpression re(
      "^[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2})?$");
  return re.match(token).hasMatch();
}

inline DecodeTarget parseDecodeTarget(const QString& message) {
  DecodeTarget target;
  const QStringList tokens = message.trimmed().split(' ', Qt::SkipEmptyParts);
  if (tokens.isEmpty()) return target;
  for (int i = 1; i < tokens.size(); ++i) {
    const QString& token = tokens[i];
    if (looksLikeGrid(token)) {
      if (target.grid.isEmpty()) target.grid = token;
      continue;
    }
    if (target.call.isEmpty() && plausibleCall(token)) target.call = token;
  }
  return target;
}

}  // namespace radio::app
