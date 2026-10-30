# 同类开源项目调研与可复用资产（2026-10-29）

- 口径：2026-10-29 经 GitHub REST API 实查（★ 数、最后 push、license.spdx_id），
  关键项再读源码头 / LICENSE 原文确认「or later」字样。
- 本仓许可：**AGPL-3.0-or-later**；兼容性判定见 §1。
- 结论先行：**没有可以「整仓合并」的项目**——同类项目要么产品正面撞车
  （wfview / wfweb / rigplane），要么领域不同（纯日志、纯 SDR）。可直接拿来用的
  粒度是**库**和**语义**：唯一现实可 vendor 的是 `ft8_lib`（MIT）。

## 1. 许可兼容矩阵（并入本仓的合法前提）

| 上游许可 | 能否并入 AGPL-3.0-or-later 仓 | 条件 |
|---|---|---|
| MIT / BSD-3 / Apache-2.0 | ✅ 可以 | 保留版权行与许可全文（`NOTICE.md` + `LICENSES/`） |
| LGPL-2.1 / LGPL-3.0 | ✅ 可以 | 动态链接或独立进程；不静态并入单体；保留替换途径 |
| GPL-3.0 | ✅ 可以 | 整仓保持 AGPL-3.0-or-later（AGPLv3 §13 允许组合），保留原作者版权 |
| GPL-2.0-**or-later** | ✅ 可以（按 GPLv3 取用） | 同上 |
| GPL-2.0-**only** | ❌ 不可以 | 与 GPLv3/AGPLv3 不兼容（如 CQRLOG） |
| 无 LICENSE | ❌ 不可以 | 只读参考（如 `BH6AAP/ic705controler`） |

本次实查的许可细节：

- **Dire Wolf** 源码头为 "GNU General Public License … version 2 … **or any later
  version**" → 可并入（`LICENSE` 文件本身只有 v2 正文，以源码头为准）。
- **aprslib** 源码头同样 "either version 2 … or (at your option) any later
  version" → 可并入（Python，仅作语义对照）。
- **kappanhang**：API 报 `NOASSERTION`，但 `LICENSE` 原文是 **MIT**
  （Copyright 2020 Norbert Varga HA2NON, Akos Marton ES1AKOS）→ 可并入。
- **rigplane-core**：**MIT**（与 WO-273 表一致）。法律上可并入，但本仓既定
  约束是「不抄 rigplane 代码、只走 issue/PR 上游协作」——许可允许 ≠ 路线允许。
- **CQRLOG**：`GPL-2.0`（无 or-later）→ 不并库，只读参考。

## 2. 项目清单：能不能直接拿过来

| 项目 | ★ | 许可 | 活跃 | 与本仓关系 | 结论 |
|---|---:|---|---|---|---|
| `wf-group/wfview` | 15（镜像） | GPL-3.0 | 2026-10 | Icom LAN/USB 桌面遥控正主；0x27 scope、音频语义对照源 | 只读参考；不并库（产品重叠、Qt 老代码体量大） |
| `adecarolis/wfweb` | 111 | GPL-3.0 | 2026-10 | headless wfview + 浏览器前端；引擎/传输/前端分层可学 | 只读参考 |
| `rigplane/rigplane-core` | 24 | MIT | 2026-10-29 | 正面竞品（Icom+Yaesu+webUI+rigctld） | 只走 issue/PR（#4352 已提）；不抄代码 |
| `nonoo/kappanhang` | 172 | MIT | 2021 停更 | RS-BA1 协议教材（50001/50003 登录与音频） | 参考；本仓已自研，无需 vendor |
| `wb2osz/direwolf` | 2065 | GPL-2.0-or-later | 2026-10 | AFSK/HDLC/比特同步/iGate 语义务实实现 | 取算法思路（§3.2），不整段移植 |
| `kgoba/ft8_lib` | 305 | MIT | 2025-08 | LDPC(174,91) + 8-FSK 解调 + 呼号 hash | **可 vendor**（§3.1） |
| `jgaeddert/liquid-dsp` | 2322 | MIT | 2026-10 | FIR/AGC/重采样/NCO | 备选：真机 AFSK 灵敏度不足时再引 |
| `Hamlib/Hamlib` | 857 | LGPL-2.1+（API 报 NOASSERTION） | 2026-10-29 | rigctld 协议权威；第三方电台抽象 | 协议对照；未来支持他厂电台时动态链接 |
| `rossengeorgiev/aprs-python` | 132 | GPL-2.0-or-later | 2023 | APRS 解析语义（本轮压缩坐标已对照） | 参考 |
| `foldynl/QLog` | 363 | GPL-3.0 | 2026-10-29 | 日志 UX / DXCC / ADIF 完整度 | 参考 |
| `ok2cqr/cqrlog` | 146 | GPL-2.0（only） | 2026-10-28 | 日志功能对照 | 只读参考（许可不可并） |
| `w1hkj/fldigi` | 173（镜像） | GPL-3.0 | 2023 | 全模式 MODEM 库思路（PSK/RTTY 等） | 参考 |
| `armel/IC705SMeter` / `IC705MultiMeter` | 26 / 44 | MIT | — | IC-705 S 表/Po/SWR 读数实现 | S 表标定对照（§3.5） |

一句话：**「合并项目」这条路不存在**；能「拿来直接用」的只有 `ft8_lib` 这类
纯算法库，其余价值都在语义与算法细节。

## 3. 建议清单（按性价比排序）

### 3.1 FT8 本地解码（`kgoba/ft8_lib`，MIT，3–5 天）

- 价值：摆脱「必须先装 WSJT-X」；解码结果直接填现有 `radio::wsjtx::Decode`
  结构 → 工作台面板零改动（只需多一个「解码源：WSJT-X UDP / 本地」开关）。
- 工作：15 s 周期环形缓冲 → 12 kHz 重采样 → 8-FSK 同步 + LDPC(174,91) 译码
  → 呼号 hash 表（`ft8_lib` 全提供）。CMake 以 `vendor/ft8_lib` 静态库接入。
- 风险：与真实 WSJT-X 同时开会出现重复解码（靠开关规避）；本地解码灵敏度
  低于 WSJT-X（后者有 a7/a8 多轮候选与 OSD），需在帮助里写明。

### 3.2 APRS 空口质量（Dire Wolf 思路，不引代码）

- **DCD / 发送前监听**：现在 `pumpAfskTx` 只按节流发射，没有「信道忙」判定；
  真机 iGate 场景容易撞包。思路：解调器已有的 `disc` 能量可做简易 DCD，
  忙时排队 1–2 s 重试。
- **早迟门比特同步**：当前是「半比特内重同步」启发式，长突发下易丢位；
  Dire Wolf 的 PLL + 早迟门更稳（约 60 行 C 代码量级，可自写）。
- **预加重/去加重**：FM 链路 6 dB/oct；发射侧加预加重可明显改善对端解调。
- 若真机灵敏度仍不足，再引 `liquid-dsp` 的 FIR/AGC（MIT，别自己造）。

### 3.3 频谱算法增量（现有 Welch 平均之上）

- **抛物线插值读频**：峰值 bin 与左右邻 bin 做三点插值，频率读数从 ±bin 宽
  收到 ±0.1 bin，弱信号点频更准。
- **时间中值滤波**：单帧脉冲噪声（火花/点击）用 3 帧中值比均值更干净；
  可与 4 帧 Welch 平均并存（先中值后平均）。
- **窗函数可选**：Blackman-Harris 4-term（-92 dB 旁瓣）用于弱信号邻道观察；
  默认仍 Hann（主瓣窄、幅度准）。
- **ENBW 校正**：噪声底读数乘窗的等效噪声带宽，S 表/底噪才与真机可比。

### 3.4 日志与集成（QLog / Hamlib 口径）

- ADIF **导入**（现在只有导出 + `LoggedADIF` 展示）；DXCC 字段校验。
- rigctld `\dump_state` 全字段 + `+` 扩展模式（Hamlib 客户端兼容度）。
- `rigctl` 的 `u`/`U`（TUNING STEP）、`s`/`S`（split）补齐后可直接被
  N1MM / fldigi 当 rig 用。

### 3.5 仪表标定（IC705SMeter 对照）

- 本轮 S 表已改 IARU 理论口径（S9=-73 dBm，6 dB/S，S9+60=-13，
  `apps/desktop/app/smeter_scale.h`）。原厂 S 表字节（CI-V `0x15 0x02`）到
  dBm 的实际曲线需真机采样；`IC705SMeter`（MIT）可作对照实现，真机复验时
  把标定表替换进 `smeter_scale.h` 即可，UI/测试不用动。

## 4. 本轮已落地的算法/正确性改动（非调研项，备查）

| 项 | 位置 | 效果 |
|---|---|---|
| 周期 Hann + 4 帧线性功率递归平均 | `libs/dsp/dsp/fft.*`、`MainWindow.cpp` | 噪底起伏 ≈ 1/2（√4），单音幅度不变；时间常数 ~0.26 s |
| S 表分段标定（刻度/指针/读数同源） | `apps/desktop/app/smeter_scale.h`、`meter_widgets.cpp` | S9 上下两段斜率不同，读数 `S6` / `S9+10` |
| WSJT-X `delta_frequency` 改有符号 qint32 | `wsjtx/wsjtx.*`、`wsjtx_panel.cpp` | 负频差不再溢出成 40 亿 Hz；双击回填按 `dial + ΔF` |
| APRS 压缩坐标 csT 三字段 1.0.1 口径 | `aprs/aprs.cpp` | 海拔/航向/速度正确；去掉南纬镜像错误 |
| iGate IS→RF 只放消息/对象/Item | `aprs/igate.cpp` | 符合 New-N 惯例，不再把位置包灌进 RF |
| 瀑布行偏移按 0.65 可见带修正 | `WaterfallWidget.cpp` | 最新行回到屏幕顶部，去掉 ~0.35 屏（~6 s @15fps）固定滞后 |
