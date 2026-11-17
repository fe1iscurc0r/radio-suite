# libsgp4（vendored）

- 上游：<https://github.com/dnwrnr/sgp4> （Apache-2.0，Copyright 2013 Daniel Warner）
- 版本：commit `b915de601a4af0a9c1c61c54efaafb168ba3defb`（2026-09-27，2026-11-16 拉取）
  - tarball sha256 `2d3c5cf92a11b713ca0f07a0100b7156291b8201e1c02ce5e3e97449f3062e5a`
    （取自 `https://api.github.com/repos/dnwrnr/sgp4/tarball/b915de60…`，`codeload` 直链本机被拦）
- 收录范围：只有 `libsgp4/`（17 个 .cpp + 17 个 .h + 上游 CMakeLists.txt）与上游 `LICENSE`。
- **未收录**：`runtest/`、`tests/`、`passpredict/`、`sattrack/`、`csvprop/`、`data/`、
  顶层 `CMakeLists.txt`、`SGP4-VER.TLE`。理由见下面「为什么判据不来自上游」。
- **本地修改：无。** `diff -r` 对上游目录逐字节一致（含上游 CMakeLists.txt，我们也不用它）。
  这条不是客套话：vendor 的算法库一旦被本地改过，官方向量对账就同时失去意义，
  所以「没动过」本身是要守的东西，改动前请先想清楚是不是该改。
- 构建：`third_party/CMakeLists.txt` 显式列出 17 个源文件编成静态库 `radio_sgp4`
  （C++17，第三方告警关闭，不参与本仓 /W4 门禁）。不用上游 CMakeLists 是因为它会同时建
  STATIC 和 SHARED 两个目标（`sgp4` / `sgp4s`），而本仓要的是单一静态目标。
- 头文件包含路径是 `third_party/sgp4`（**不是** `third_party/sgp4/libsgp4`）：
  上游源码内部一律 `#include <libsgp4/XXX.h>`，所以暴露父目录才能原样编译。
- 依赖：纯标准库（`<cmath> <charconv> <chrono> <cstdint> <sstream> <stdexcept>` 等），
  不碰 Qt。所以安卓 arm64 交叉编译与三平台 CI 都直接编它，没有额外开关。

## 为什么判据不来自上游

上游 `runtest/runtest.cpp` 只按官方格式**打印**计算值，与期望值的比对是外部 `diff` 做的
——判据不在代码里。自跑自比等于没验，所以本仓的验收把期望值从**另一家实现**带进来：

- `tests/fixtures/sgp4/SGP4-VER.TLE` 与 `tests/fixtures/sgp4/tcppver.out`
  取自 <https://github.com/brandon-rhodes/python-sgp4>（**MIT**，commit 分支 `master`，
  tarball sha256 `5a7b177136765cb725b59371e749d0980a618532c62c1628580520d160e4fd34`）。
  `tcppver.out` 是 Vallado 官方 C++ 参考实现（`sgp4unit`）的输出，33 例 / 667 行。
- 许可原文：本目录 `LICENSE-upstream-Apache-2.0.txt`（libsgp4）与
  `tests/fixtures/sgp4/LICENSE-upstream-MIT.txt`（向量数据），
  并同步到仓库根 `LICENSES/Apache-2.0.txt`、`LICENSES/MIT.txt`。
- 对账用例：`tests/test_sgp4_vectors.cpp`。门限 1 m / 1e-4 km/s，具名例外两个
  （23599 的 Lyddane/AcTan 分歧、33333/33334 的错误码探针），依据都写在文件头注释里。

## 拿来做什么

卫星页（待办 #99）：TLE 解析 → SGP4 传播 → `Observer`/`CoordGeodetic`/`CoordTopocentric`
求方位/仰角/距离 → 多普勒 → 双 VFO 收发频率跟踪。本目录只进「传播与坐标」这一层，
UI 与电台编排都在 `apps/desktop/`，不反过来依赖 vendored 码。
