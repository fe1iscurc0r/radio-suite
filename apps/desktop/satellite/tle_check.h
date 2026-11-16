// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · satellite/tle_check.h — TLE 输入边界校验（纯标准库）
//
// 为什么本仓要自己查校验和：libsgp4 的 Tle 验长度、行首、星号一致、字段能不能解析，
// 但**不验 mod-10 校验和**。粘贴错位一位数字（最常见的手误）它照样吞下去，
// 于是卫星页安静地跟着一颗不存在的星——比报错难查得多。
//
// 这里只验、只报，不替用户「修」：长度不对就说是长度不对，不悄悄补空格或补校验和。
// 静默修复会把坏输入变成看起来好但含义不同的输入，那是更难追的一类错。
//
// 校验和约定（在本仓的官方向量语料上实测过：33 条里 30 条通过，
// 不通过的恰好是 33333/33334/33335 三条合成错误码探针）：
//   对第 1..68 列求和，数字按面值、'-' 记 1、其余字符记 0，mod 10 后与第 69 列比。
#pragma once

#include <string>
#include <vector>

namespace radio::satellite {

struct TleCheck {
  bool ok{};
  std::string line1, line2;   // 规范化（去 \r 与行尾空白）后的两行，可直接交给 libsgp4
  std::vector<std::string> errors;
  // 校验和单独留数：UI 要能指出「第 1 行应为 4，实到 7」，只说「校验和错」没用。
  int expect1{-1}, found1{-1};
  int expect2{-1}, found2{-1};
};

// 逐条累加错误，不在第一个问题上短路：用户一次粘贴往往同时犯长度和校验和两个错。
TleCheck check_tle(const std::string& line1, const std::string& line2);

}  // namespace radio::satellite
