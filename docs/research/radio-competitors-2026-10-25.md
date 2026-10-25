# 无线电双端竞品盘点（WO-273 阶段一 · A+B 节）

- 日期：2026-10-25 · 执行：WO-268 领取人（即本单执行者，见施工记录 §0 的授权说明）
- 口径：★ 数以工单预核表为准（2026-10-25 实查）；追加项经 GitHub API/官网实查
- 结论四选一：`红海-不碰` / `只读参考` / `可融合（许可允许）` / `真空-可做`
- 本仓许可背景：radio-suite = AGPL-3.0；rsba1-core = MIT（**禁 GPL 入内**）

## 表① Icom / RS-BA1 协议层

| 项目 | ★ | 许可 | 语言 | 最后 push | 体积 | 平台 | 覆盖能力（≤20 字） | 结论 |
|---|---|---|---|---|---|---|---|---|
| `fe1iscurc0r/rsba1-core`（自有） | 0 | MIT | Python | 2026-10-09 | 238KB | 跨平台 | RS-BA1 控制闭环 + MCP，无音频/频谱 | 真空-可做（补音频/0x27 即为本体） |
| `nonoo/kappanhang` | 172 | NOASSERTION | Go | 2026-05-26 停更 | 56MB | CLI | RS-BA1 客户端鼻祖，音频半成品 | 只读参考（协议教材，2021 停更） |
| `rigplane/rigplane-core` | 23 | MIT | Python | 2026-10-25 | 97MB | 跨平台+Web | Icom LAN/USB + Yaesu + webUI + rigctld | 红海-不碰（正面撞车，见 C 节） |
| `hamrec/cp705` | 7 | MIT | C | 2026-08-16 | 51MB | 嵌入式 | IC-705 控制协议 C 实现 | 只读参考（协议事实校对源） |

## 表② 桌面 SDR / 全模式工作站

| 项目 | ★ | 许可 | 语言 | 最后 push | 体积 | 平台 | 覆盖能力 | 结论 |
|---|---|---|---|---|---|---|---|---|
| `adecarolis/wfweb` | 110 | GPL-3.0 | C++ | 2026-10-24 | 76MB | 桌面/浏览器/Docker | headless wfview：浏览器全模式工作站 | 红海-不碰（许可亦隔离） |
| `wf-group/wfview` | 15（镜像） | GPL-3.0 | C++ | 2026-10-21 | — | 桌面 | Icom LAN/USB 桌面遥控正主 | 只读参考 |
| `dividebysandwich/sdroxide` | 274 | GPL-3.0 | Rust | 2026-10-23 | 115MB | 桌面 | Rust SDR 多后端 | 只读参考（架构可学） |
| `aethersdr/AetherSDR` | 246 | GPL-3.0 | C++ Qt6 | 2026-10-25 | 283MB | 桌面 | Qt6 全功能 SDR 工作站 | 只读参考（Qt6 模块划分可学） |
| `kd9taw/Nexus` | 113 | GPL-3.0 | Rust | 2026-10-25 | 133MB | 桌面 | Rust 电台工作站 | 只读参考 |
| `boybook/tx-5dr` | 81 | GPL-3.0 | TypeScript | 2026-10-25 | 49MB | Web | X6100/TX-500 Web 遥控 | 只读参考 |

## 表③ 手机端

| 项目 | ★ | 许可 | 语言 | 最后 push | 体积 | 平台 | 覆盖能力 | 结论 |
|---|---|---|---|---|---|---|---|---|
| `BH6AAP/ic705controler` | 17 | 无 LICENSE | Kotlin | 2026-09-04 | — | Android | IC-705 蓝牙/串口控制壳 | 只读参考（无许可禁抄） |
| `rkarikari/9GRadio` | 55 | NOASSERTION | Kotlin | 2026-10-23 | — | Android | 多协议遥控（闭源混合） | 只读参考 |
| `lquesada/qFT8` | 28 | NOASSERTION | — | 2026-10-25 | — | Android | FT8 手机端 | 只读参考 |
| **Android 开源 RS-BA1 客户端** | — | — | — | — | — | — | **GitHub 无结果；Play 仅闭源** | **真空-可做（本单最大赌注，先证伪需求）** |

## 表④ 日志与集成

| 项目 | ★ | 许可 | 语言 | 最后 push | 体积 | 平台 | 覆盖能力 | 结论 |
|---|---|---|---|---|---|---|---|---|
| 本仓 logbook（HamLog 融合） | — | AGPL-3.0 | C++ | 本仓 | — | Windows | 直读写 HamLog.db + ADIF | 真空-可做（agent+日志闭环独此一家） |
| Log4OM / QLog 等传统日志 | — | 各异 | 各异 | 活跃 | — | 桌面 | 全功能日志站 | 红海-不碰（只做 ADIF 对接） |

## 六层能力矩阵（B 节）

规则：✅ 必附证据；无证据降 ⚠️。六层 = 协议 / 音频 / 频谱 / UI / 模式 / 集成。

| 项目 | 协议 | 音频 | 频谱 | UI | 模式 | 集成 |
|---|---|---|---|---|---|---|
| **rsba1-core**（自有） | ✅ RS-BA1 全控制（radio_link.py 真机 E2E） | ⚠️ 50003 仅握手维活，无 codec/PCM（grep 无 audio codec 证据） | ❌ 0x27 一行没有 | ❌ 无 | ❌ 无 | ✅ MCP 32 工具（`python -m rsba1.mcp`） |
| **rigplane-core** | ✅ Icom LAN/USB+Yaesu CAT（README profiles；rigctld golden-replay 测试） | ✅ audio/ 层 + AudioCapable 协议（README；RX/TX 细节 ⚠️ 未实读源码） | ✅ scope+waterfall（Desktop/LCD skins；FPS/GPU ⚠️ 未证实） | ✅ 自带 Svelte5 webUI 四皮肤（README 明示"no Electron"） | ⚠️ 无数字模式（README 未列 FT8 等） | ✅ rigctld 内置；❌ 无 MCP |
| **wfweb** | ✅ CI-V USB/LAN（wfview 引擎全继承） | ✅ 浏览器 RX + 浏览器麦克风 TX（README 明示） | ✅ 浏览器 waterfall + 解码标注（README） | ✅ 浏览器全功能 + Web Serial 独立版 | ✅ FT8/JS8/CW/FreeDV/AX.25（README，WASM codecs） | ⚠️ rigctld 可选+REST+WSJT-X UDP；❌ 无 MCP |
| **AetherSDR** | ⚠️ SoapySDR 系（README 未实读，>100MB 禁 clone） | ✅ SDR 本职 | ✅ SDR 本职 | ✅ Qt6 桌面 | ✅ SDR 全家 | ❌ 无 agent/rigctld 证据 |
| **sdroxide** | ⚠️ Rust SDR 后端抽象（README 级） | ✅ SDR 本职 | ✅ SDR 本职 | ⚠️ TUI/GUI（README 级） | ⚠️ | ❌ |
| **Nexus** | ⚠️ | ⚠️ | ⚠️ | ⚠️ Rust 工作站（README 级） | ⚠️ | ❌ |
| **kappanhang** | ✅ RS-BA1 客户端鼻祖（Go，源码） | ⚠️ 半成品（2021 停更） | ❌ | ❌ CLI | ❌ | ❌ |
| **radio-suite（本仓 M2）** | ✅ RS-BA1 V2 UDP 全链（C++ 原生+字节级互锁测试，commit 99b6050）+ mock 真机语义 | ⚠️ 输出直出已通（RtAudio WASAPI，SSTV 一键发射）；**输入链未做** | ⚠️ FFT 瀑布已上屏（合成谱）；0x27 真机帧未接 | ✅ Qt6 桌面（原厂视觉+仪表三件套） | ✅ SSTV 8 制式（编解码回环）+ DTMF + APRS 收发 | ✅ **MCP=rsba1-core 桥（后手）+ HTTP 总线 /api/state·spectrum（curl 验收）+ HamLog 共库 + ADIF** |
| **Android 全家（表③）** | ❌ 无开源 RS-BA1 | ❌ | ❌ | ⚠️ 闭源壳 | ⚠️ qFT8 仅 FT8 | ❌ |

矩阵一句话：**协议层红海（rigplane/wfview/wfweb 全占），音频+频谱在桌面侧是硬件短板各不相同，而「Android 端 RS-BA1」「agent+日志闭环」「headless 可嵌总线」三格全场空白。**
