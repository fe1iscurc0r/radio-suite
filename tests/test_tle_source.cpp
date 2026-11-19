// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tests/test_tle_source.cpp — W2.2 星历来源（纯离线）
// 判据：三行组解析、坏行进隔离区不静默丢弃（A-2.3）、缓存往返、时效判定。
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "satellite/tle_source.h"

namespace fs = std::filesystem;

namespace {

// 官方向量语料里的真实条目（与 test_sgp4_vectors/test_tle_check 同源）
const char kName[] = "DELTA 1 DEB";
const char kL1[] =
    "1 06251U 62025E   06176.82412014  .00008885  00000-0  12808-3 0  3985";
const char kL2[] =
    "2 06251  58.0579  54.0425 0030035 139.1568 221.1854 15.56387291  6774";

std::string triplet(const std::string& name, const std::string& l1,
                    const std::string& l2) {
  return name + "\n" + l1 + "\n" + l2 + "\n";
}

}  // namespace

TEST_CASE("tle source: catalog parse and quarantine", "[satellite]") {
  using namespace radio::satellite;
  // 2 条好 + 1 条校验和坏（第 1 行末位翻到 6）+ 1 条残组（缺 L2）
  std::string text = triplet(kName, kL1, kL2);
  std::string bad = triplet("BAD SUM", std::string(kL1).substr(0, 68) + "6",
                            kL2);
  bad += "ORPHAN NAME\n1 06251U 62025E   06176.82412014  .00008885  00000-0  12808-3 0  3985\n";
  const auto rep = parse_tle_catalog(text + bad);

  CHECK(rep.good.size() == 1);
  CHECK(rep.rejected.size() == 2);
  CHECK(rep.rejected[0].raw.find("BAD SUM") != std::string::npos);
  CHECK(rep.rejected[0].why.find("校验和") != std::string::npos);
}

TEST_CASE("tle source: store roundtrip and stale", "[satellite]") {
  using namespace radio::satellite;
  const auto tmp = fs::temp_directory_path() / "radio-suite-tle-store-test";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  TleStore store(tmp);

  ParseReport rep;
  rep.good.push_back({kName, kL1, kL2});
  constexpr int64_t kNow = 1'700'000'000;
  REQUIRE(store.saveCatalog("visual", rep, kNow));

  std::vector<TleEntry> out;
  int64_t src = 0;
  REQUIRE(store.loadCatalog("visual", &out, &src));
  REQUIRE(out.size() == 1);
  CHECK(out[0].name == kName);
  CHECK(out[0].line1 == kL1);
  CHECK(out[0].line2 == kL2);
  CHECK(src == kNow);

  // 时效：新鲜 → 不过期；3 天前 → 过期；时钟倒挂（now < source）→ 过期
  CHECK_FALSE(TleStore::isStale(kNow, kNow, 2 * 60));
  CHECK(TleStore::isStale(kNow + 3 * 86400, kNow, 2 * 60));
  CHECK(TleStore::isStale(kNow - 100, kNow, 2 * 60));

  // 隔离区文件已生成（本轮 rep 无拒绝，不应有隔离区文件）
  CHECK_FALSE(fs::exists(tmp / "quarantine" / "visual.txt"));
  fs::remove_all(tmp, ec);
}
