// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <vector>

namespace radio::spike {

struct ScopeWireFrame {
  uint8_t receiver = 0;
  uint8_t mode = 0;
  uint32_t start_freq_hz = 0;
  uint32_t end_freq_hz = 0;
  uint16_t sequence = 0;
  bool out_of_range = false;
  std::vector<uint8_t> pixels;
};

bool decodeScopeFrame(const QByteArray& binary, ScopeWireFrame* out);
QByteArray makeCommandEnvelope(const QString& id, const QString& name,
                               const QByteArray& params_json);

}  // namespace radio::spike
