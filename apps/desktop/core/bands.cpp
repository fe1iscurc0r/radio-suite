// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/bands.h"

namespace radio::core {
namespace {

// 参考实现的基线三段（1.8-30 / 50-54 / 144-148 MHz，见
// vendor/rsba1-core/src/rsba1/ctypes_wrappers/civ_commands.py AMATEUR_BANDS）
// 与中国业余常用划分合并后的表。全部闭区间。
constexpr Band kTable[] = {
    {"160m",    1'800'000,    2'000'000},
    {"80m",     3'500'000,    3'900'000},
    {"40m",     7'000'000,    7'200'000},
    {"30m",    10'100'000,   10'150'000},
    {"20m",    14'000'000,   14'350'000},
    {"17m",    18'068'000,   18'168'000},
    {"15m",    21'000'000,   21'450'000},
    {"12m",    24'890'000,   24'990'000},
    {"10m",    28'000'000,   29'700'000},
    {"6m",     50'000'000,   54'000'000},
    {"2m",    144'000'000,  148'000'000},
    {"70cm",  430'000'000,  440'000'000},
    {"23cm", 1'240'000'000, 1'300'000'000},
};

}  // namespace

std::span<const Band> amateurBands() {
  return std::span<const Band>(kTable, sizeof(kTable) / sizeof(kTable[0]));
}

bool isAllowedFreq(uint64_t hz) { return bandFor(hz).has_value(); }

bool isRxAllowed(uint64_t hz) {
  // 原厂接收范围（RS-BA1 RemoteController/models/IC-705.ini [FREQUENCY]）：
  // RANGE0 = 30kHz–199,999,999 Hz；RANGE1 = 400–470 MHz。
  // 注：IC-705 无 1.2GHz 段；23cm 行在白名单表里是给后续电台的，收听不可达。
  constexpr uint64_t kRx0Lo = 30'000, kRx0Hi = 199'999'999;
  constexpr uint64_t kRx1Lo = 400'000'000, kRx1Hi = 470'000'000;
  return (hz >= kRx0Lo && hz <= kRx0Hi) || (hz >= kRx1Lo && hz <= kRx1Hi);
}

// 常用接收波段（非业余，收听用；区间取自业余惯例划分，中心=区间中点）
std::span<const Band> rxBroadcastBands() {
  static constexpr Band kRx[] = {
      {"AIR",   118'000'000, 137'000'000},  // 民航话音 AM
      {"BC-FM",  87'500'000, 108'000'000},  // 调频广播
      {"BC-SW",     5'800'000,  15'600'000}, // 短波广播（49/31m 等带）
  };
  return std::span<const Band>(kRx, sizeof(kRx) / sizeof(kRx[0]));
}

std::optional<Band> bandFor(uint64_t hz) {
  for (const Band& b : amateurBands())
    if (hz >= b.lo_hz && hz <= b.hi_hz) return b;
  return std::nullopt;
}

}  // namespace radio::core
