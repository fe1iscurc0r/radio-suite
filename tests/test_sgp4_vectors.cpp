// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tests/test_sgp4_vectors.cpp
// vendored libsgp4 的架构级验收：拿 Vallado 官方 C++ 参考实现的输出（tcppver.out，MIT）
// 逐行对账我们自己编出来的 ECI 位置/速度。
//
// 为什么不能只跑 SGP4-VER.TLE 自己看它「没崩」：上游 runtest 只按官方格式打印计算值，
// 比对是外部 diff 做的，判据不在代码里。自跑自比等于没验。
// 这条用例证明的是「这份 vendored 源码在本仓的编译器/编译选项/常量下算得对」，
// 而不是「SGP4 模型准」——后者由向量集本身覆盖不到（模型误差量级 1–3 km/天）。
// 容差与具名例外的依据写在下面 namespace 里，那里把「先定粗门 → 实测 → 收紧」说全了。

#include <libsgp4/Eci.h>
#include <libsgp4/SGP4.h>
#include <libsgp4/SatelliteException.h>
#include <libsgp4/Tle.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace {

// 容差怎么定的，把过程写清楚，别只留一个数字：
// 先按「远小于真实模型误差（1–3 km/天）、又远大于常量与浮点差异」定了 1 km 的粗门。
// 跑完发现这粗门几乎是被**一例**外部分歧顶着的（实测 0.964 km），而其余 30 例与参考
// 实现一致到 ≤ 9e-6 km。留着 1 km 等于用一例外部分歧去掩护另外 30 例的任何回退，
// 所以按实测把通用门**收紧**到 1 m / 1e-4 km/s，并给那一例单独具名放行。
// 方向要说明确：门限数值来自测量，但这里是「测完收紧」，不是「测完放宽把红的改成绿的」。
//
// 30 例里实测最大 9e-6 km（23333），1 m 的门留了约 100 倍余量：常量错、EOP 错、
// 文件装错、编译选项把 double 降级——任何一种都会立刻爆门。
// 门放在哪一档不是嘴上说的：把 Globals.h 的 kMU 从 398600.8 改成 398601.8
// （相对误差 2.5e-6），实测位置偏差立刻涨到 6–8.5 m，用例 FAIL；改回即 257/257 绿。
// 也就是说 1 m 正好卡在「基线一致性 9 mm」与「极小常量错误 6 m」之间。
constexpr double kMaxPositionErrorKm = 0.001;
constexpr double kMaxVelocityErrorKmS = 0.0001;

// 23599（ARIANE 42P+3 R/B）具名单独判。官方向量文件给它注的原话是
//   "## fig Lyddane bug at > 280.5 min for AcTan()"
// —— 这一例的用途就是暴露 Lyddane 小周期 / AcTan 分支在几小时之后各实现走法不同，
// 不是用来比精度的。实测剖面（本机本次）：400 min 以内与参考实现一致到 1 m 以内，
// 420–720 min 偏差 0.26–0.96 km（峰值 0.964 km @460 min），速度峰值 3.9e-4 km/s。
// 2 km / 0.01 km/s 比实测松，但仍远紧于「整例不看」——真把深空分支建坏到几公里以上，
// 照样抓得住。不按 tsince 切分例外边界：那等于把例外刻在测量分界线上。
constexpr unsigned int kLyddaneProbeNorad = 23599;
constexpr double kLyddaneMaxPositionErrorKm = 2.0;
constexpr double kLyddaneMaxVelocityErrorKmS = 0.01;

// 向量文件自己的注释把另两例标成「故意触发错误码」的探针输入，不是真实卫星：
//   33333 —— "# check error code 4"          （bstar=0.25992681、e=0.995、周期 1070 min）
//   33334 —— "# try and check error code 2"  （平均运动 0.00001、周期 6752 min）
// 官方输出里 33334 只有 1 行，且数值与 33333 的最后一行逐字节相同 —— 那是参考实现报错之后
// 把上一例的状态原样打出来的残留，不是它算出的轨道。拿残留当期望值没有意义，所以这两例
// 完全不进紧门；但**仍然要求算得出有限值**（或按设计抛它自己的 SatelliteException），
// 真把库建坏了照样抓得住。它们与参考实现的实际偏差也照样量出来打印，不藏。
constexpr unsigned int kProbe33333 = 33333;
constexpr unsigned int kProbe33334 = 33334;

std::string fixture(const std::string& name) {
  return std::string(RADIO_SUITE_FIXTURES_DIR) + "/sgp4/" + name;
}

struct TlePair {
  std::string line1, line2;
};

std::vector<TlePair> readTlePairs(const std::string& path) {
  std::ifstream f(path);
  CAPTURE(path);
  REQUIRE(f.is_open());
  std::vector<TlePair> out;
  std::string line;
  TlePair pending;
  const size_t kLen = libsgp4::Tle::LineLength();
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    // 向量文件的第二行后面挂着「起始/结束/步长」三列测试参数，Tle 只吃前 69 字符
    // （上游 runtest 就是 substr(0, LineLength())，本用例照做；参数列本用例用不上，
    // 因为要算的 tsince 直接从官方输出里取）。
    if (line.size() > kLen) line = line.substr(0, kLen);
    if (line.size() < kLen) continue;
    if (line[0] == '1') {
      pending = {line, ""};
      continue;
    }
    if (line[0] == '2' && !pending.line1.empty()) {
      pending.line2 = line;
      out.push_back(pending);
      pending = {"", ""};
    }
  }
  return out;
}

struct ExpectedRow {
  double tsince;
  double pos[3];
  double vel[3];
};

bool parseRow(const std::string& line, ExpectedRow* out) {
  std::istringstream ss(line);
  return !!(ss >> out->tsince >> out->pos[0] >> out->pos[1] >> out->pos[2] >>
            out->vel[0] >> out->vel[1] >> out->vel[2]);
}

// 官方输出按「<NORAD> xx」分块，块内每行是 tsince + ECI 位置(km) + 速度(km/s)
// （后面还跟半长轴/轨道要素等列，本用例不需要，读前 7 个字段即可）。
// 块头认「含 x」而不是「全是数字加 xx」：数据行一个字母都没有，而头行的 xx 本身就是字母，
// 拿字符集去判会把每一行都当成数据行（第一版就是这么 0 块的）。
struct GoldenBlock {
  unsigned int norad{};
  std::vector<ExpectedRow> rows;
};

std::vector<GoldenBlock> readGoldenBlocks(const std::string& path) {
  std::ifstream f(path);
  CAPTURE(path);
  REQUIRE(f.is_open());
  std::vector<GoldenBlock> blocks;
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.find('x') != std::string::npos) {
      std::istringstream ss(line);
      unsigned int norad = 0;
      std::string marker;
      if (ss >> norad >> marker && marker == "xx") blocks.push_back({norad, {}});
      continue;
    }
    ExpectedRow row{};
    if (parseRow(line, &row) && !blocks.empty()) blocks.back().rows.push_back(row);
  }
  return blocks;
}

double positionError(const double got[3], const double want[3]) {
  return std::sqrt(std::pow(got[0] - want[0], 2) + std::pow(got[1] - want[1], 2) +
                   std::pow(got[2] - want[2], 2));
}

}  // namespace

TEST_CASE("sgp4: vendored libsgp4 reproduces the official reference output",
          "[sgp4][vectors]") {
  const auto tles = readTlePairs(fixture("SGP4-VER.TLE"));
  const auto golden = readGoldenBlocks(fixture("tcppver.out"));

  // 结构先钉住：解析器一旦和文件格式错位，后面的对账就全是噪声
  CHECK(tles.size() == 33);
  CHECK(golden.size() == tles.size());
  REQUIRE(tles.size() == golden.size());

  size_t compared = 0;
  size_t probe_rows = 0;
  double worst_pos_km = 0.0, worst_vel_kms = 0.0;
  double probe_worst_pos_km = 0.0;
  double lyddane_worst_pos_km = 0.0, lyddane_worst_vel_kms = 0.0;
  double lyddane_worst_tsince = 0.0;
  std::string worst_case;

  for (size_t i = 0; i < tles.size(); ++i) {
    CAPTURE(i);
    libsgp4::Tle tle(tles[i].line1, tles[i].line2);
    libsgp4::SGP4 model(tle);
    const std::string id = std::to_string(tle.NoradNumber());
    // 块头自带 NORAD 号，逐块核对：只数块数的话，顺序错位也能凑出同样的总数
    CHECK(golden[i].norad == tle.NoradNumber());

    REQUIRE_FALSE(golden[i].rows.empty());
    const bool probe = tle.NoradNumber() == kProbe33333 ||
                       tle.NoradNumber() == kProbe33334;
    for (const auto& want : golden[i].rows) {
      double got_pos[3] = {0, 0, 0}, got_vel[3] = {0, 0, 0};
      try {
        const libsgp4::Eci eci = model.FindPosition(want.tsince);
        const auto p = eci.Position();
        const auto v = eci.Velocity();
        got_pos[0] = p.x;
        got_pos[1] = p.y;
        got_pos[2] = p.z;
        got_vel[0] = v.x;
        got_vel[1] = v.y;
        got_vel[2] = v.z;
      } catch (const libsgp4::SatelliteException& e) {
        // 探针例：官方注释说它们就是「check error code 4 / 2」，抛异常是合格结果。
        // 33334 尤其要说清——它在官方输出里那一行与 33333 的末行逐字节相同，
        // 也就是参考实现对它同样没算出东西，只是把上一例的状态原样打了出来；
        // 所以这里没有可比的东西，不能拿那行残留当期望值判我们「不一致」。
        if (probe) {
          INFO("probe " << id << " tsince " << want.tsince
                        << " threw as designed: " << e.what());
          ++probe_rows;
          continue;
        }
        INFO("case " << id << " tsince " << want.tsince << ": " << e.what());
        FAIL("vendored SGP4 threw where the reference produced a value");
      }
      const double dp = positionError(got_pos, want.pos);
      const double dv = std::sqrt(std::pow(got_vel[0] - want.vel[0], 2) +
                                  std::pow(got_vel[1] - want.vel[1], 2) +
                                  std::pow(got_vel[2] - want.vel[2], 2));
      if (probe) {
        // 不进紧门，但算出来的必须是有限值：哪天连数都吐不出来，那就是真坏了。
        for (const double v : {got_pos[0], got_pos[1], got_pos[2], got_vel[0],
                               got_vel[1], got_vel[2]})
          CHECK(std::isfinite(v));
        if (dp > probe_worst_pos_km) probe_worst_pos_km = dp;
        ++probe_rows;
        continue;
      }
      // 23599 走它自己的具名上限，所以不能混进「通用门的实测最坏值」：混进去以后，
      // 那 30 例真正的一致性（≤ 9e-6 km）会被这一例的 0.96 km 顶掉，报告里的数字
      // 就不再代表紧门到底紧到什么程度。
      const bool lyddane = tle.NoradNumber() == kLyddaneProbeNorad;
      if (lyddane) {
        if (dp > lyddane_worst_pos_km) {
          lyddane_worst_pos_km = dp;
          lyddane_worst_tsince = want.tsince;
        }
        if (dv > lyddane_worst_vel_kms) lyddane_worst_vel_kms = dv;
      } else {
        if (dp > worst_pos_km) {
          worst_pos_km = dp;
          worst_case = id + " @" + std::to_string(want.tsince);
        }
        if (dv > worst_vel_kms) worst_vel_kms = dv;
      }
      // 单行也要判：只看最大值的话，一处爆掉会被「平均起来还行」掩盖的写法
      // 就不存在了，但反过来只报总数也看不出是哪例，所以两条都留。
      INFO("row: case " << id << " tsince " << want.tsince << " dp = " << dp
                        << " km, dv = " << dv << " km/s");
      CHECK(dp < (lyddane ? kLyddaneMaxPositionErrorKm : kMaxPositionErrorKm));
      CHECK(dv < (lyddane ? kLyddaneMaxVelocityErrorKmS
                          : kMaxVelocityErrorKmS));
      ++compared;
    }
  }

  INFO("通用紧门（30 例，上限 1 m）实测最坏 = " << worst_pos_km
       << " km（" << worst_case << "），速度最坏 = " << worst_vel_kms << " km/s");
  INFO("具名例外 23599（上限 2 km）实测最坏 = " << lyddane_worst_pos_km
       << " km @ tsince " << lyddane_worst_tsince
       << " min，速度最坏 = " << lyddane_worst_vel_kms << " km/s");
  INFO("错误码探针 33333/33334：行数 = " << probe_rows
       << "，与参考实现偏差最坏 = " << probe_worst_pos_km << " km（不参与判定）");
  INFO("紧门行数 = " << compared);
  
  // 667 行官方输出里，33333 占 5 行、33334 占 1 行，剩下 661 行进紧门
  CHECK(compared == 661);
  CHECK(probe_rows == 6);
}
