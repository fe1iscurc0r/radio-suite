// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tests/test_tle_check.cpp
// TLE 输入校验的验收。判据主要吃仓库里已有的官方向量语料（33 对），
// 不自己造「我以为对」的样本——语料本身就是独立来源。

#include "satellite/tle_check.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string fixture(const std::string& name) {
  return std::string(RADIO_SUITE_FIXTURES_DIR) + "/sgp4/" + name;
}

struct Pair {
  std::string id, l1, l2;
};

std::vector<Pair> read_corpus() {
  std::ifstream f(fixture("SGP4-VER.TLE"));
  REQUIRE(f.is_open());
  std::vector<Pair> out;
  std::string line, pending;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (line.size() < 69) continue;  // 向量文件在第二行后面挂了测试参数
    if (line[0] == '1') {
      pending = line.substr(0, 69);
      continue;
    }
    if (line[0] == '2' && !pending.empty()) {
      out.push_back({pending.substr(2, 5), pending, line.substr(0, 69)});
      pending.clear();
    }
  }
  return out;
}

// 官方向量里的 DELTA 1 DEB：已实测校验和正确。
const char kGood1[] =
    "1 06251U 62025E   06176.82412014  .00008885  00000-0  12808-3 0  3985";
const char kGood2[] =
    "2 06251  58.0579  54.0425 0030035 139.1568 221.1854 15.56387291  6774";

std::string dump(const std::vector<std::string>& v) {
  std::string o;
  for (const auto& e : v) o += "[" + e + "] ";
  return o;
}

bool has_error(const std::vector<std::string>& v, const std::string& needle) {
  return std::any_of(v.begin(), v.end(), [&](const std::string& e) {
    return e.find(needle) != std::string::npos;
  });
}

std::string pad69(std::string s) {
  s.resize(69, ' ');
  return s;
}

}  // namespace

TEST_CASE("tle check: official vector corpus validates, probes do not",
          "[satellite][tle][corpus]") {
  const auto corpus = read_corpus();
  REQUIRE(corpus.size() == 33);

  std::vector<std::string> failed;
  size_t passed = 0;
  for (const auto& p : corpus) {
    const auto r = radio::satellite::check_tle(p.l1, p.l2);
    if (r.ok) {
      ++passed;
      continue;
    }
    failed.push_back(p.id);
    // 通过的例不该有残留错误，失败的例必须报得出是哪一行
    INFO("corpus " << p.id << " errors: " << [&] {
      std::ostringstream o;
      for (const auto& e : r.errors) o << e << " | ";
      return o.str();
    }());
  }

  // 实测：33 条里 30 条过 mod-10；不过的恰好是三条合成错误码探针。
  // 探针连校验和都是假的，这和它们在参考实现上算不出轨道是同一件事——
  // 也就是说「校验和能挡住假输入」这条不是设计愿望，是语料里量出来的。
  CHECK(passed == 30);
  CHECK(failed == std::vector<std::string>{"33333", "33334", "33335"});
}

TEST_CASE("tle check: a single flipped digit is caught and the delta is reported",
          "[satellite][tle]") {
  const auto clean = radio::satellite::check_tle(kGood1, kGood2);
  REQUIRE(clean.ok);
  // 通过时 expect==found，报出来的数才有意义
  CHECK(clean.expect1 == clean.found1);
  CHECK(clean.expect2 == clean.found2);

  // 把偏心率那一列的 0030035 改成 0030036（手误改一位），校验和必须跟不上。
  std::string bad2(kGood2);
  const size_t pos = bad2.find("0030035");
  REQUIRE(pos != std::string::npos);
  bad2[pos + 6] = '6';
  const auto r = radio::satellite::check_tle(kGood1, bad2);
  CHECK_FALSE(r.ok);
  CHECK(r.expect2 != r.found2);
  // 数字 +1 ⇒ 期望校验和 +1（mod 10），且不等于末位实填值：这条不重新实现规则
  CHECK(r.expect2 == (clean.expect2 + 1) % 10);
  CHECK(r.found2 == clean.found2);
  REQUIRE_FALSE(r.errors.empty());
  CHECK(r.errors[0].find("校验和") != std::string::npos);
}

TEST_CASE("tle check: paste noise is normalized, not silently repaired",
          "[satellite][tle]") {
  // 行尾 \r\n 与尾随空格是粘贴/文件里最常见的噪声：规范化后应通过。
  const auto r = radio::satellite::check_tle(std::string(kGood1) + "\r\n",
                                             std::string(kGood2) + "   \n");
  CHECK(r.ok);
  CHECK(r.line1.size() == 69);
  CHECK(r.line2.size() == 69);

  // 但少一列（68 字符，等于校验位被截掉）不补——补出来的是「看着合法而含义不同」的输入。
  const std::string short2 = std::string(kGood2).substr(0, 68);
  const auto s = radio::satellite::check_tle(kGood1, short2);
  CHECK_FALSE(s.ok);
  CHECK(s.line2.size() == 68);  // 原样交出去，不擅自加长
  CHECK(s.errors[0].find("长度") != std::string::npos);
}

TEST_CASE("tle check: structural problems are each reported independently",
          "[satellite][tle]") {
  // 星号不一致：两行都合法长度、行首正确，但号不同。
  // 注意改号会连带把校验和也算错，所以这里必然同时有两条错误——
  // 断言找的是「星号那条在不在」，不是「只有这一条」：两件事独立报告才是要验的性质。
  std::string l2mismatch(kGood2);
  l2mismatch.replace(2, 5, "06252");
  auto r = radio::satellite::check_tle(kGood1, l2mismatch);
  REQUIRE_FALSE(r.ok);
  CHECK(has_error(r.errors, "星号"));

  // 行首错：用一整行合法长度、只把行首换成 '3'。
  std::string bad_first(kGood1);
  bad_first[0] = '3';
  const auto bf = radio::satellite::check_tle(bad_first, kGood2);
  INFO("bad_first: " + dump(bf.errors));
  CHECK_FALSE(bf.ok);
  CHECK(has_error(bf.errors, "行首"));

  // 末位不是数字：报「校验位不是数字」，不去算 mod 10 得出一个假期望值。
  std::string nonnum(kGood1);
  nonnum[68] = 'X';
  const auto rr = radio::satellite::check_tle(nonnum, kGood2);
  CHECK_FALSE(rr.ok);
  CHECK(has_error(rr.errors, "不是数字"));
  CHECK(rr.expect1 == -1);

  // 一次粘贴同时犯两个错（星号 + 因此而来的校验和），两条都要报，不在第一个就短路。
  std::string two(kGood1);
  two.replace(2, 5, "00001");
  const auto both = radio::satellite::check_tle(two, kGood2);
  INFO("both: " + dump(both.errors));
  CHECK_FALSE(both.ok);
  CHECK(both.errors.size() >= 2);
}

TEST_CASE("tle check: padding a short line to 69 columns is not a repair",
          "[satellite][tle]") {
  // 这条是写测试时自己踩出来的：上一版用 resize(69,' ') 造「整行」，
  // 于是校验器把它剥回 7 字符再报长度错——剥掉尾随空白本来就是设计意图，
  // 所以这里把它固化成一条判据，而不是改测试去迁就错误的构造方式。
  const auto padded = radio::satellite::check_tle(pad69("3 06251"), kGood2);
  INFO("padded: " + dump(padded.errors));
  CHECK_FALSE(padded.ok);
  CHECK(has_error(padded.errors, "长度"));
  CHECK(padded.line1.size() == 7);  // 规范化后原样交出，不拿补白当合法
}
