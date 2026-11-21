# 2026-11-21 · A-2.1 过境预报交叉核对（ISS / SO-50，北京默认站）

## 目的与判据

WO-275 §5.7 A-2.1：过境预报必须**外部对账**，判据 AOS/TCA/LOS 误差 ≤ 1 min、最大仰角 ≤ 1°。
自算自比不算验——本目录是「另一份独立实现复算同一组 TLE」的逐过境比对。

## 现场

- 被测实现：本仓 `apps/desktop/satellite/sat_track.cpp`（vendored dnwrnr/libsgp4），
  经 scratch harness `satpass_scratch.cpp` 直接链 `radio_satellite.lib` + `radio_sgp4.lib` 调用
  `find_passes()`。星历门限 = 仰角 ≥ 5°，窗口 = 从「此刻」起 24 h。
- 对照实现：**satellite.js 5.0.0**（MIT，python-sgp4 的 JS 移植，独立代码库）。
  同一组 TLE、同一站、同一仰角门限；10 s 粗扫 + 二分到亚秒定 AOS/LOS，1 s 细扫定 TCA/最大仰角。
- 站坐标：**应用默认站**（`station/lat=39.9, lng=116.4, alt_km=0.05`，`satellite_panel.cpp:272-277`），
  不是任何人的真实 QTH。判据不依赖具体位置，但结论只对这个站成立。
- 星历：Celestrak `gp.php?CATNR=...&FORMAT=tle`，拉取时间 2026-11-21 13:0x（UTC+8）：
  - ISS (ZARYA) 25544，历元 2026-11-20 21:09:47 UTC
  - SAUDISAT 1C (SO-50) 27607，历元 2026-11-20 10:45:27 UTC
  原件 `iss.tle` / `so50.tle` 同目录。

## 结果（`compare-out.txt`）

| 卫星 | 过境数 | ΔAOS | ΔLOS | ΔTCA | Δ最大仰角 | Δ最小斜距 |
|---|---|---|---|---|---|---|
| ISS | 6 | 0.0 s（全部） | 0.0 s（全部） | ≤ 0.5 s | ≤ 0.001° | ≤ 0.2 km |
| SO-50 | 5 | 0.0 s（全部） | 0.0 s（全部） | ≤ 0.5 s | ≤ 0.001° | ≤ 0.2 km |

判据 ≤ 1 min / ≤ 1°，实测余量三个数量级。ISS 6 个过境最大仰角 11.5°–54.4°，
SO-50 5 个过境 8.1°–59.9°，高低仰角都有覆盖。

## 这条证据能说明什么、不能说明什么

- **能**：本仓这条 `find_passes()` 链路（TLE 解析 → SGP4 → 站心几何 → 过境定界/细化）在实现层面没有错，
  与另一份独立移植在同一天同一组 TLE 上给出同一批过境。
- **不能**：「SGP4 模型物理准」。两份实现同源（Vallado SGP4 的各语言移植），
  0.0 s 级别的 AOS/LOS 一致是**移植一致性**，不是模型精度。模型本身的误差量级仍按 1–3 km/天量级，
  真机对空还要叠加 TLE 时效与时钟。
- 没有采用「网站公开预报」对账：网站通常给到分钟且带缓存/取整，做 1 min 门噪声太大；
  独立实现可复现、可离线、可进 CI。两者的差异写在这里，不藏。

## 复现命令

```bat
:: 1) 编译 scratch harness（在含 radio_satellite.lib / radio_sgp4.lib 的构建树旁）
call "%VS%\VC\Auxiliary\Build\vcvars64.bat"
cl /nologo /std:c++17 /EHsc /O2 /W0 /MD /utf-8 /I apps\desktop /I third_party\sgp4 ^
   satpass_scratch.cpp build\apps\desktop\satellite\radio_satellite.lib build\third_party\radio_sgp4.lib
:: 2) 本仓结果
satpass.exe iss.tle 39.9 116.4 0.05 24 > ours_iss.txt
satpass.exe so50.tle 39.9 116.4 0.05 24 > ours_so50.txt
:: 3) 独立实现对照（Node 20+）
npm i satellite.js@5.0.0
node compare.js > compare-out.txt
```

`compare.js` 里窗口起点按 C++ 那一次运行的实际 `tsince`（ISS 495.6 min / SO-50 1119.9 min）对齐；
列出的 11 个过境都远离窗口边界，起点差几分钟不参与判定。

## 现场采集的额外发现（写进台账，供后续接手）

- `satellite.js` v5 的 `invjday(jd)` 返回的是 `Date` 而不是对象，直接按 `{year,mon,...}` 用会得到
  `Invalid Date` → 全部过境算 0 个（第一版就是这么空表的）。TLE 历元直接从第 1 行
  `yy=2000+slice(18,20)`、`doy=slice(20,32)` 换算更稳，不受库版本影响。
