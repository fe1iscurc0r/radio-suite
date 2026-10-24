// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/bands.h — 业余频段白名单（全仓唯一权威，安全门禁引用此处）
#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace radio::core {

struct Band {
  const char* name;  // 波段惯用名
  uint64_t lo_hz;    // 闭区间下界
  uint64_t hi_hz;    // 闭区间上界
};

// 中国大陆业余业务常用频段表。
// ⚠️ M1 复核项：与工信部现行《业余无线电台管理办法》及 CRAC 频率规划逐条核对，
//    并按操作证书等级（A/B/C 类）标注附加限制；当前为 M0 公认列表。
std::span<const Band> amateurBands();

// 频率是否落在任一业余频段（闭区间）——**发射白名单**（TX 唯一口径）。
bool isAllowedFreq(uint64_t hz);
inline bool isTxAllowed(uint64_t hz) { return isAllowedFreq(hz); }

// 接收是否允许：收听不限于业余段（广播/航空接收合法），只受电台接收范围约束。
// IC-705 接收范围约 500kHz–1.45GHz，此处取宽容下界 100kHz。
bool isRxAllowed(uint64_t hz);

// 命中频段；不在白名单返回 nullopt。
std::optional<Band> bandFor(uint64_t hz);

}  // namespace radio::core
