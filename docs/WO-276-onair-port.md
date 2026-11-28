# WO-276 · 接入 OnAir 清单（移植优先版，2026-11-27 起）

> 前提：师傅判定「自己重写的效果不如 OnAir 原版」。本单因此**以移植为主、重写为例外**，
> 但只移植**自包含的算法与渲染件**，不搬它的 ImGui/GLFW 应用栈。
> 目标仓：radio-suite（AGPL-3.0-or-later）。上游：adswill/OnAir（GPL-3.0-or-later）。

## §0 许可口径（每批 PR 的硬门槛）

- **GPLv3 §13 单向兼容**：GPLv3 作品可以与 AGPLv3 作品链接/合并，合并整体按 AGPLv3 走；
  反向不允许。我们本就是 AGPL，因此**可以合法移植**。
- 每个移植件必须同时具备：
  1. 源文件保留 `SPDX-License-Identifier: GPL-3.0-or-later` 与原作者版权行；
  2. `NOTICE.md` 增条目：上游仓库 + 文件路径 + 取用时的 commit；
  3. About/许可页保留 OnAir 的 §7(b) 署名行
     `Based on OnAir by Kristijonas Adomaitis - https://on-air.dev` 并致谢贡献者；
  4. `docs/licensing-onair.md` 登记「文件 → 上游 commit → 改动摘要」。
- **不拿**：OnAir 名称与 logo（商标不在 GPL 内）；语音编解码（AMBE/HDC 专利，且上游也不随包）；
  `THIRD_PARTY.md` 中任何非 GPL 兼容部件。
- 上游有 ARGUS 代码溯源扫描：保留声明与署名不是道义，是硬要求。

## §1 移植原则

- **只拿自包含件**：一个文件（或一小簇）+ 明确输入输出 + 能写同源回归，才允许 vendor。
- **不搬应用栈**：ImGui、GLFW、他们的 `gfx.h` 抽象一律不引进；渲染在我们 Qt 的
  `WaterfallWidget` 里做，只搬算法与参数。
- **同源回归**：每件都要有可复跑对照（他们的生成信号或 `files.on-air.dev` 公开 IQ 样本），
  不许「看起来一样」。

## §2 批次

### P1 频谱 / 瀑布（师傅点名，先做）

| 项 | 内容 |
|---|---|
| 拿什么 | `core/src/dsp/` 里频谱相关：FFT 窗与尺寸、多 bin→像素抽稀策略、峰值保持/衰减；颜色映射表（LUT） |
| 怎么拿 | 抽稀与峰保逻辑按原样移植进 `libs/dsp`；LUT 若为第三方色表按色表自身许可；渲染用 QtGL 重画（不搬 heatmap 绘制栈） |
| 验收 | ① 同源 IQ 同一参数下：dB→色查表逐点一致；抽稀结果逐 bin 一致；② 帧率不低于现版；③ 峰值保持衰减曲线数值对照 |
| 先做 | **基准对比实验**：拿样本库一段 IQ，记录两边参数（FFT 尺寸/重叠/窗/动态范围/抽稀），把差距量化成表，再决定移植哪几件 |

### P2 IQ 源与录制格式（接电视棒的前提）

| 项 | 内容 |
|---|---|
| 拿什么 | 文件源解析：cs8 / cu8 / cf32 / ci16 / cu16 / SigMF / 双声道 WAV |
| 自己写 | `IIqSource` 抽象（块流 + 中心频率/采样率/格式/时间戳），与现有 `IAudioLink`/`ICivTransport` 同构；`rtl_tcp` 源（TCP、零驱动） |
| 验收 | ① 样本库文件回放，频谱与 OnAir 截图同参数对照；② rtl_tcp 空口 30 分钟无丢块、无重排错误；③ 回放结果幂等（连跑 3 次逐 bin 一致） |

### P3 模式注册表

| 项 | 内容 |
|---|---|
| 拿什么 | `core/src/engine/modes.*` 的表结构（一行一种模式：名称/族/带宽/解调入口/测试信号） |
| 怎么拿 | 移植**表结构与注册 API**，不搬 392 个解码器；现有 FT8/APRS/SSTV/DTMF/CW 先迁表 |
| 验收 | ① 迁移后全量 ctest 绿；② 新增一个 dummy 模式仅靠表登记就出现在选择器（证明表驱动成立） |

### P4 信号工作台最小版

- 质量分（lock/SNR/频偏）、BPSK/QPSK 星座图、测量读数；从 `analysis_tabs.cpp`/`plot.cpp` 取思路，自己实现。
- 验收：合成信号下星座图肉眼可分四相、质量分随 SNR 单调。

### P5 扫描器

- 频道扫描（scan tab 思路）+ 现有 APRS 地图复用；验收：样本 IQ 上扫出预设频道列表。

### 明确不接

Docker headless / OnAir Web（我们 bus、rigctld、MCP、PWA 已覆盖）· 插件 SDK · Rust 协议层 ·
ImGui/GLFW/gfx 整栈 · 专利语音编解码 · 他们的遥测/上传服务。

## §3 风险登记

| 风险 | 影响 | 缓解 |
|---|---|---|
| 仓 1.1 GB，目录耦合深 | 抽取单件成本高 | 只按文件清单取，不 clone 全量；以 commit 定位 |
| 他们的「生成信号验证」≠ 空口 | 移植来的是纸面正确 | 每件附同源 IQ 回归；空口结论单独标 |
| 色表/LUT 许可 | 可能不是 GPL | 查色表来源；非兼容则换等价开源色表 |
| 驱动依赖（Zadig/WinUSB、Soapy） | 用户装不上 | rtl_tcp 优先，Soapy 后置且默认关 |
| ARGUS 溯源 | 漏署名会被追 | §0 四条作为 PR 模板 checklist |

## §4 顺序

`P1 基准对比 → P1 移植 → P2（并行备料）→ P3 → P4/P5 视收益`。
每批一个分支一个 PR，PR 正文必须贴 §0 的四项许可动作与同源回归输出。