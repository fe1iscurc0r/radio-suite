// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · satellite/tle_check.cpp
#include "satellite/tle_check.h"


namespace radio::satellite {
namespace {

constexpr size_t kLineLen = 69;

std::string normalize(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                         s.back() == ' ' || s.back() == '\t'))
    s.pop_back();
  return s;
}

int mod10(const std::string& body) {
  int sum = 0;
  for (const char c : body) {
    if (c >= '0' && c <= '9')
      sum += c - '0';
    else if (c == '-')
      sum += 1;
  }
  return sum % 10;
}

// 返回值只表示「这行结构上还能不能继续查」（长度与行首），不含校验和结论：
// 校验和错和星号不一致是彼此独立的检查，绑在一起会让校验和失败时悄悄跳过星号比对。
bool check_line(const std::string& s, char want_first, std::vector<std::string>& err,
                const char* which, int* expect, int* found) {
  bool structural = true;
  if (s.size() != kLineLen) {
    err.push_back(std::string(which) + " 长度 " + std::to_string(s.size()) +
                  "，应为 69（去掉行尾空白后）");
    structural = false;
  }
  if (s.empty() || s[0] != want_first) {
    err.push_back(std::string(which) + " 行首不是 '");
    err.back().push_back(want_first);
    err.back() += "'";
    structural = false;
  }
  if (!structural) return false;
  const char last = s[kLineLen - 1];
  if (last < '0' || last > '9') {
    err.push_back(std::string(which) + " 第 69 列校验位不是数字");
    return true;
  }
  *expect = mod10(s.substr(0, kLineLen - 1));
  *found = last - '0';
  if (*expect != *found) {
    err.push_back(std::string(which) + " 校验和应为 " + std::to_string(*expect) +
                  "，实到 " + std::to_string(*found));
  }
  return true;
}

std::string sat_number(const std::string& s) {
  if (s.size() < kLineLen) return "";
  return s.substr(2, 5);
}

}  // namespace

TleCheck check_tle(const std::string& raw1, const std::string& raw2) {
  TleCheck out;
  out.line1 = normalize(raw1);
  out.line2 = normalize(raw2);

  const bool ok1 = check_line(out.line1, '1', out.errors, "第 1 行", &out.expect1,
                             &out.found1);
  const bool ok2 = check_line(out.line2, '2', out.errors, "第 2 行", &out.expect2,
                              &out.found2);

  // 星号一致只有在两行都够长时才有意义；长度错已经单独报了，这里不重复。
  if (ok1 && ok2) {
    auto trim = [](std::string v) {
      while (!v.empty() && v.front() == ' ') v.erase(v.begin());
      while (!v.empty() && v.back() == ' ') v.pop_back();
      return v;
    };
    const std::string n1 = trim(sat_number(out.line1));
    const std::string n2 = trim(sat_number(out.line2));
    if (n1 != n2)
      out.errors.push_back("两行星号不一致：'" + n1 + "' vs '" + n2 + "'");
  }

  out.ok = out.errors.empty();
  return out;
}

}  // namespace radio::satellite
