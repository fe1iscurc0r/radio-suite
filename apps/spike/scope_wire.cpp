// SPDX-License-Identifier: AGPL-3.0-or-later
#include "apps/spike/scope_wire.h"

namespace radio::spike {

bool decodeScopeFrame(const QByteArray& binary, ScopeWireFrame* out) {
  if (binary.size() < 16) return false;
  const auto* d = reinterpret_cast<const uint8_t*>(binary.constData());
  if (d[0] != 0x01) return false;
  out->receiver = d[1];
  out->mode = d[2];
  out->start_freq_hz = uint32_t(d[3]) | (uint32_t(d[4]) << 8) |
                       (uint32_t(d[5]) << 16) | (uint32_t(d[6]) << 24);
  out->end_freq_hz = uint32_t(d[7]) | (uint32_t(d[8]) << 8) |
                     (uint32_t(d[9]) << 16) | (uint32_t(d[10]) << 24);
  out->sequence = uint16_t(d[11]) | (uint16_t(d[12]) << 8);
  out->out_of_range = (d[13] & 0x01) != 0;
  const int pixels = int(d[14]) | (int(d[15]) << 8);
  if (binary.size() < 16 + pixels) return false;
  out->pixels.assign(d + 16, d + 16 + pixels);
  return true;
}

QByteArray makeCommandEnvelope(const QString& id, const QString& name,
                               const QByteArray& params_json) {
  return QByteArray("{\"type\":\"cmd\",\"id\":\"") + id.toUtf8() +
         "\",\"name\":\"" + name.toUtf8() +
         "\",\"params\":" + params_json + "}";
}

}  // namespace radio::spike
