// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · satellite/tle_source.h — 星历来源：缓存/校验/隔离区/时效（纯标准库）
//
// W2.2 口径（总纲 §5.3）：默认不联网（拉取在 app 层做）；星历逐条过 check_tle，
// 坏条目进 quarantine/ 并计数，不静默丢弃、不自动修；缓存带来源时间戳，
// 过期与否由上层按时效标记，过境表置灰是 UI 的职责（W2.3）。
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "tle_check.h"

namespace radio::satellite {

struct TleEntry {
  std::string name;
  std::string line1, line2;
};

// 三行一组（Celestrak gp.php?FORMAT=tle 形状：名字 + "1 " 行 + "2 " 行）。
// 解析只吃前 69 字符（TLE 第 2 行后可能挂测试参数列，总纲 §15）。
// 坏条目带原始文本进 rejected，不静默丢弃。
struct ParseReport {
  struct Rejected {
    std::string raw;   // 原始行组，供隔离区回放
    std::string why;   // check_tle 的错误列表（拼接）
  };
  std::vector<TleEntry> good;
  std::vector<Rejected> rejected;
};

ParseReport parse_tle_catalog(const std::string& text);

// 缓存文件头：`# radio-suite tle cache  group=<g>  source_unix=<秒>`
// 隔离区：<dir>/quarantine/<group>.txt，拒绝数 > 0 时追加（带 why 行）。
class TleStore {
 public:
  explicit TleStore(std::filesystem::path dir);

  // 写缓存；拒绝数 > 0 时把 rejected 追加进隔离区文件（不覆盖历史隔离区）。
  bool saveCatalog(const std::string& group, const ParseReport& rep,
                   int64_t source_unix, std::string* error = nullptr);

  // 读缓存。失败（无文件/头损坏）返回 false 并给 error。
  bool loadCatalog(const std::string& group, std::vector<TleEntry>* out,
                   int64_t* source_unix, std::string* error = nullptr);

  // 时效判定：age = now − source_unix（分钟）。NaN/负 age 一律算过期（防时钟倒挂）。
  static bool isStale(int64_t now_unix, int64_t source_unix, double max_age_min) {
    const double age_min = (now_unix - source_unix) / 60.0;
    return !(age_min >= 0) || age_min > max_age_min;
  }

 private:
  std::filesystem::path dir_;
};

}  // namespace radio::satellite
