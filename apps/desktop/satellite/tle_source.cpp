// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · satellite/tle_source.cpp
#include "tle_source.h"

#include <fstream>
#include <sstream>

namespace radio::satellite {
namespace {

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(line);
  }
  return out;
}

}  // namespace

ParseReport parse_tle_catalog(const std::string& text) {
  ParseReport rep;
  const auto lines = splitLines(text);
  size_t i = 0;
  while (i < lines.size()) {
    // 跳过空行与注释行
    if (lines[i].empty() || lines[i][0] == '#') {
      ++i;
      continue;
    }
    TleEntry e;
    e.name = lines[i];
    // 名字行后必须紧跟 "1 " 与 "2 " 两行，缺一即整组拒绝
    if (i + 2 >= lines.size() || lines[i + 1].size() < 2 ||
        lines[i + 1][0] != '1' || lines[i + 1][1] != ' ' ||
        lines[i + 2].size() < 2 || lines[i + 2][0] != '2' ||
        lines[i + 2][1] != ' ') {
      std::string raw;
      raw += lines[i] + "\n";
      for (size_t j = i + 1;
           j < lines.size() && j <= i + 2 && !(lines[j][0] == '#' || lines[j].empty());
           ++j)
        raw += lines[j] + "\n";
      rep.rejected.push_back({raw, "不完整的三行组"});
      i += 1;
      // 再同步：跳过残组的 "1 "/"2 " 行，避免把孤立数据行当成新名字再拒一次
      while (i < lines.size() && !lines[i].empty() && lines[i][0] != '#' &&
             lines[i].size() > 1 &&
             (lines[i][0] == '1' || lines[i][0] == '2') && lines[i][1] == ' ')
        ++i;
      continue;
    }
    e.line1 = lines[i + 1].substr(0, 69);
    e.line2 = lines[i + 2].substr(0, 69);
    const auto c = check_tle(e.line1, e.line2);
    if (!c.ok) {
      std::string why;
      for (const auto& s : c.errors) {
        if (!why.empty()) why += "; ";
        why += s;
      }
      rep.rejected.push_back({e.name + "\n" + e.line1 + "\n" + e.line2 + "\n", why});
      i += 3;
      continue;
    }
    rep.good.push_back(std::move(e));
    i += 3;
  }
  return rep;
}

TleStore::TleStore(std::filesystem::path dir) : dir_(std::move(dir)) {}

bool TleStore::saveCatalog(const std::string& group, const ParseReport& rep,
                           int64_t source_unix, std::string* error) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  const auto cat = dir_ / (group + ".txt");
  std::ofstream f(cat, std::ios::trunc);
  if (!f) {
    if (error) *error = "写缓存失败: " + cat.string();
    return false;
  }
  f << "# radio-suite tle cache  group=" << group
    << "  source_unix=" << source_unix << "\n";
  for (const auto& e : rep.good)
    f << e.name << "\n" << e.line1 << "\n" << e.line2 << "\n";
  f.close();
  if (!rep.rejected.empty()) {
    const auto q = dir_ / "quarantine";
    std::filesystem::create_directories(q, ec);
    std::ofstream qf(q / (group + ".txt"), std::ios::app);
    if (qf) {
      qf << "---- source_unix=" << source_unix << " 拒绝 " << rep.rejected.size()
         << " 条 ----\n";
      for (const auto& r : rep.rejected)
        qf << r.raw << "[why] " << r.why << "\n";
    }
  }
  return true;
}

bool TleStore::loadCatalog(const std::string& group, std::vector<TleEntry>* out,
                           int64_t* source_unix, std::string* error) {
  const auto cat = dir_ / (group + ".txt");
  std::ifstream f(cat);
  if (!f) {
    if (error) *error = "无缓存: " + cat.string();
    return false;
  }
  std::string first;
  std::getline(f, first);
  int64_t src = 0;
  // 头行：# radio-suite tle cache  group=...  source_unix=...
  const auto pos = first.find("source_unix=");
  if (first.rfind("# radio-suite tle cache", 0) != 0 || pos == std::string::npos) {
    if (error) *error = "缓存头损坏: " + first;
    return false;
  }
  src = std::stoll(first.substr(pos + 12));
  TleEntry e;
  std::string l1, l2;
  while (std::getline(f, e.name) && std::getline(f, l1) && std::getline(f, l2)) {
    e.line1 = l1.substr(0, 69);
    e.line2 = l2.substr(0, 69);
    out->push_back(e);
  }
  if (source_unix) *source_unix = src;
  return true;
}

}  // namespace radio::satellite
