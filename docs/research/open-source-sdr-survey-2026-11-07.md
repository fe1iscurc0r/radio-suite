# 全网开源 SDR 生态调研（2026-11-07）

> WO-273 业余域 13 项竞品盘点（`radio-competitors-2026-10-25.md`）的扩展版：本次不限业余无线电，
> 覆盖**全部开源 SDR 生态**——桌面工作站 / Web 远程 / 框架库 / 硬件固件 / 专用解码器 / 移动端六域。
> 方法：主线程 WebSearch 逐域检索 + 子代理对 GitHub / GitLab / osmocom Gitea API 直查
> （★ 数、许可、最后 push 均为 2026-11-07 原始核实，非转述）。
> 用途：为 radio-suite 的路线校准与功能取舍提供全景参照；每节末尾给「与本仓的关联」。

---

## 0. 结论速览（TL;DR）

1. **业余电台遥控本仓所在的「窄赛道」正在升温**：rigplane Core 3.0 已正式发布（Python 库 + Web 界面，MIT），
   2026 年新出现 Rigflow（Rust, MIT, UDP 实时 DSP）、SDRoxide、VLSC 一批同类——验证了 WO-273 选路正确，但也意味着窗口期在收窄。
2. **桌面通用接收的天花板格局未变**：SDR++（rolling 1.2.1 + Android 预发布）与 SDRangel（功能最全）双雄，
   GQRX 稳守 Linux 朴素接收，SigDigger 卡位信号分析；CubicSDR 事实上被淘汰。
3. **框架层正在换代**：GNU Radio 4.0 到 RC3（2026-10-13，异构计算/GPU/FPGA/AI 目标），FutureSDR（Rust）仍是实验态，
   SoapySDR 依旧是不可动摇的硬件抽象事实标准。
4. **AI 已经进入解调链路**：FreeDV RADE（AI 声码器，2025-06 正式发布）进入 HF 数字语音主流；
   NVIDIA Sionna 代表神经接收机研究侧；出现 RTL-ML、OnAir（120 解码模式整合）等应用侧新苗。
5. **单用途解码器的架构收敛成范式**：幅度域批处理 + 每硬件一个后端文件 + 自描述解码器注册表 + 分级可回放流水线
   （dump1090/readsb、rtl_433、SatDump 是三个最完整的教科书）。
6. **许可卫生两极分化**：解码器清一色 GPL；对外库倾向宽松（LGPL/BSD/MIT）；遗留项目仓库内无 LICENSE 是生态主要污点。

---

## 1. 桌面工作站域

| 项目 | 2026 现状 | 定位 | 备注 |
|------|-----------|------|------|
| **SDRangel** | 活跃（本仓早前已详查：MCP server、卫星追踪、channel 化架构） | 功能最全的桌面 SDR+收发 | channel 插件式架构与 300+ 解调器是其护城河 |
| **SDR++** | rolling 1.2.1；Android 预发布 | 轻快、bloat-free 通用接收 | SoapySDR + 专用源模块；SIMD DSP；全插件化。**Brown fork**（sannysanoff）在官方之上加内建 FT8/FT4、KiwiSDR/WebSDR 直连、降噪——2025 年可见的 feature 活跃大多发生在 fork 与三方插件，不在官方。HydraSDR fork 带 Module Manager |
| **GQRX** | 活跃维护 | Linux 朴素接收「SDR# 开源答案」 | GNU Radio + Qt；模拟解调为主，收听稳定性口碑最佳 |
| **SigDigger** | v0.3.0（2025-04） | 信号分析/逆向（非纯接收） | Suscan 引擎；OpenGL 瀑布；盲信号分析、突发检测、Time Slider 离线回放；DragonOS 预装 |
| **CubicSDR** | 停滞（最后 0.2.7，仅 crash 修复） | — | SoapySDR canvas UI 曾经流行，依赖已落后于兼容性，用户流向 SDR++/GQRX |
| **QSpectrumAnalyzer** | 维护中 | 纯频谱观测小工具 | 非竞品，属辅助 |
| **Thetis（openHPSDR）** | 官方放缓（2023-11）；社区分支 eu2av Enhanced 活跃（2026-08） | openHPSDR Protocol 1/2 主控 | 见 §4 A8 |

**与本仓关联**：本仓不是通用接收机而是「电台控制台」，直接竞品是 rigplane/Rigflow（§2）而非本域；
但 SDR++ 的插件注册表与 SigDigger 的回放分析（Time Slider）都是本仓 dsp 层可借鉴的形态（见 §8）。

---

## 2. Web / 远程接收域

| 项目 | 2026 现状 | 架构要点 | 备注 |
|------|-----------|----------|------|
| **rigplane Core 3.0** | **已正式发布**（MIT） | Python 库（Icom 网络/USB + Yaesu FTX-1）+ Web 界面 | 本仓 WO-274 路线 A 的 sidecar 引擎；3.0 发布确认上游在投入。rigctl 兼容未见官方文档明示 |
| **OpenWebRX+** | 非常活跃（commit 以周计） | 多用户 Web 接收机（原 OpenWebRX 的增强分支，fms.komkon.org/OWRX） | 内建 SSTV(multimon-ng)/SELCALL 解码、降噪、扫描器、快捷键、树莓派一键镜像；原生 + Soapy 双源 |
| **ka9q-radio** | 活跃（KA9Q Phil Karn） | **IP 组播多信道框架**：一切信号/控制走组播，LAN 内多模块共享一个前端 | 快卷积信道化；RX888 MkII/RSP1a/RTL；ka9q-web（2025-05）补 S 表/缩放滑杆；sdrutah.org 用作 WebSDR 多波段前端（384/768 kHz）。架构上是「SDR 操作系统」的开放实现 |
| **KiwiSDR 网络** | 持续运营 | 分布式网络接收机（闭源服务端 + 开放 API/前端生态） | SDR++ Brown / 各 Web 客户端可直接挂 |
| **Rigflow** | 2026-06 新发布（Rust, MIT） | 客户端/服务器网络化 HF 收发，UDP 实时 DSP | 本赛道新进者——再次印证窄赛道升温 |
| **SDRoxide / VLSC** | 2026 新兴 | Rust 高性能 SDR 收发（原生+Web 遥控）/ 浏览器+手机+iOS 遥控生态 | 同上，观察名单 |

**与本仓关联**：本仓 bus_server（/api/state、/api/spectrum + SSE、/api/control）与 OpenWebRX+ 的 Web 契约
（多用户、频谱快照流）同构；ka9q-radio 的「一个前端多消费方」组播模型与本仓「session 唯一出口 + 总线广播」
是同一思想的两端实现。rigplane 3.0 正式版落地后 WO-274 sidecar 集成可从开发版切到稳定版。

---

## 3. 框架与 DSP 库域

| 项目 | 2026 现状 | 要点 |
|------|-----------|------|
| **GNU Radio 4.0** | **RC3（2026-10-13）**，官方称「第一个 GNU Radio 4 release」；3.x 维护线继续 | 从 fair-acc/gnuradio4 原型转正为官方仓库（2026-08）；2026-09 加入 OCUDU 生态基金会；GRCon 定调异构计算/GPU/FPGA 加速、AI-enabled SDR。SoapySDR 直接支持仍是长青 open issue（#2718） |
| **FutureSDR** | 实验态（crates.io 明示 breaking changes 预期） | Rust 异步运行时、GPU/FPGA buffer；2026-05 大重构（内部更干净，API 基本不变） |
| **SoapySDR** | 稳定 | 供应商中立硬件 API 事实标准；Python/Go 绑定齐全（Go 缺 direct buffer access） |
| **codec2 / FreeDV RADE** | RADE 2025-06 正式发布，FreeDV 2.4.1（2025-10），QST 2026-11 刊载 | **AI 声码器**（Radio Autoencoder）+ 经典 DSP 同步/信道估计；HF 数字语音带宽小于 SSB；已进 popular radios 集成 |
| **VOLK / liquid-dsp** | 稳定维护 | SIMD 内核库 / C 信号处理工具箱，无版本动荡 |
| **Sionna (NVIDIA)** | 活跃（1.6k★） | 神经接收机/AI-RAN 研究平台（GPU、Jupyter）；代表研究侧 |
| **gr-lora_sdr** | 1.0k★ | LoRa 全 PHY 收发的 GNU Radio 参考实现（开源 LoRa 反制的基准） |

**与本仓关联**：本仓 libs/dsp 自持 FFT/瀑布/WATERFALL（C++20 自研，无 GR 依赖）——GR4 的 RC 状态再次确认
WO-268 不把运行时押在 GR 上的决策正确（GR4 稳定前 API 会继续动）；RADE 若走 PC 端集成，本仓 audio 通道
（48k/16bit）具备接入 FreeDV GUI 的形态条件，可列为远期数字语音选项。

---

## 4. 硬件 / 固件 / 驱动域

> 子代理对 GitHub/GitLab/osmocom Gitea API 逐仓核实（2026-11-07）；"push" = 最后一次推送。

| # | 项目 | 仓库 | ★ | 许可 | 最后 push | 2026 现状 |
|---|------|------|-----|------|-----------|-----------|
| A1 | HackRF | greatscottgadgets/hackrf | 8,202 | GPL-2.0 | 2026-11-06 | 活跃；固件 v2026.01.3；**HackRF Pro（100k–6 GHz）2025-09 发货** |
| A2 | bladeRF | Nuand/bladeRF | 1,383 | 分组件（HDL/固件 MIT、libbladeRF LGPL-2.1、cli GPLv2） | 2026-10-02 | 维护中；release 2025.10 |
| A3a | LimeSuite（旧栈） | myriadrf/LimeSuite | 529 | Apache-2.0 | 2026-06-03 | 功能冻结（v22.09 后仅补丁） |
| A3b | **LimeSuiteNG（继任）** | myriadrf/LimeSuiteNG | 69 | Apache-2.0 | 2026-11-03 | 活跃；C++ API + SoapyLMS，支持 Mini 2.0/litePCIe |
| A4a | PlutoSDR 固件 | analogdevicesinc/plutosdr-fw | 480 | 混合拼盘 | 2026-08-13 | 稳定维护 |
| A4b | libiio | analogdevicesinc/libiio | 655 | LGPL-2.1 | 2026-11-05 | 活跃（v1.x 重构后持续迭代） |
| A5a | osmocom rtl-sdr | gitea.osmocom.org/sdr/rtl-sdr | —（自建 Gitea） | GPL-2.0 | 2026-08-11 | 缓慢维护，仍是事实标准基建 |
| A5b | RTL-SDR Blog 分支 | rtlsdrblog/rtl-sdr-blog | 519 | GPL-2.0 | 2026-08-10 | 活跃；V3/V4 增强驱动——「厂商 fork 驱动」双源模式 |
| A6a | Airspy host | airspy/airspyone_host | 280 | GPL（仓库无 LICENSE 文件） | 2026-07-19 | 维护中；HF+ 固件 5.0.1；Ranger（0.5k–2.6 GHz）未发售 |
| A6b | AirspyHF+ | airspy/airspyhf | 184 | BSD-3-Clause | 2026-07-22 | 维护中 |
| A7 | Hermes-Lite 2 | softerhardware/Hermes-Lite2 | 384 | 仓库无 LICENSE（社区按 GPL v3+ 处理，声明在官网） | 2026-11-04 | 活跃；openHPSDR Protocol 2；Makerfabs 批次生产 500+ 台 |
| A8a | Thetis | TAPR/OpenHPSDR-Thetis | 93 | 无 LICENSE 文件 | 2023-11-18 | 放缓；社区分支 eu2av/OpenHPSDR-Thetis-Enhanced（2026-08） |
| A8b | pihpsdr | g0orx/pihpsdr | 160 | GPL-2.0 | 2026-06-03 | 维护中；树莓派独立运行 Protocol 1&2 |

**点评**：
- **HackRF 是开放硬件生态健康度之首**：GPL 全栈 + 文档站 + 新硬件迭代；其 `firmware/host` 三层目录是驱动层组织教科书。
- **bladeRF 的分组件 COPYING 清单**（逐层列许可）是混合许可硬件仓库的范本；反例是 Airspy/Thetis/Hermes-Lite 的
  「仓库无 LICENSE、声明外置或缺席」——本仓未来若拆 dsp 库单发，要学 bladeRF 不学后三者。
- **LimeSuite→LimeSuiteNG** 是「开源栈换代」的可效仿迁移：旧仓冻结可读、新仓显式给出迁移路径。
- **libiio 的属性树抽象**（设备上下文 + 通道属性读写，USB/Ethernet 统一）与 CI-V 的串行帧协议对照鲜明——
  radio-suite 若未来支持 PlutoSDR 类直连 SDR，libiio 优于裸 USB。

---

## 5. 专用解码器域

> 同一子代理核实。本域是 2025–2026 开源 SDR 生态中架构最收敛、可借鉴密度最高的部分。

| # | 项目 | 仓库 | ★ | 许可 | 最后 push | 2026 现状 |
|---|------|------|-----|------|-----------|-----------|
| B1a | dump1090（antirez 原版） | antirez/dump1090 | 2,965 | BSD-3-Clause | 2026-02-15 | 休眠（原型价值） |
| B1b | dump1090-fa | flightaware/dump1090 | 1,152 | GPL-2.0 | 2026-11-03 | 活跃；`sdr.c` 后端抽象 + starch 运行时 SIMD |
| B1c | **readsb** | wiedehopf/readsb | 706 | GPL-3+ | 2026-11-06 | 高频活跃；ADS-B 生产主力（百万消息/天） |
| B2 | **rtl_433** | merbanan/rtl_433 | 7,915 | GPL-2.0 | 2026-10-27 | 非常活跃；release 25.12；~380 协议条目 |
| B3 | rtlamr | bemasher/rtlamr | 2,556 | AGPL-3.0 | 2026-11-03 | 活跃；**库优先**（Go library 而非仅 CLI） |
| B4a | dumphfdl | szpajder/dumphfdl | 162 | GPL-3.0 | 2026-05-31 | 活跃；HFDL 多信道 |
| B4b | dumpvdl2 | szpajder/dumpvdl2 | 260 | GPL-3.0 | 2026-10-20 | 活跃；VDL Mode 2 |
| B5 | acarsdec | TLeconte/acarsdec | 380 | 无 LICENSE | 2025-07-31 | **GitHub 仓库已归档**——生态转向 airframes.io 系/acars_router |
| B6a | noaa-apt | martinber/noaa-apt | 659 | GPL-3.0 | 2024-02-10 | 休眠（功能完备） |
| B6b | **SatDump** | SatDump/SatDump | 2,494 | GPL-3.0 | 2026-11-03 | 非常活跃（7.6k 提交）；稳定 1.2.2，2.0 重构中 |
| B7 | gr-satellites | daniestevez/gr-satellites | 1,000 | GPL-3.0 | 2026-11-06 | 活跃；业余卫星解码全家桶 |
| B8 | SatNOGS | GitLab librespacefoundation/* | — | AGPL/GPL 分组件 | client 2026-10-29 | 活跃；**GitHub org 是 2018 冻结镜像，实际开发在 GitLab** |
| B9a | SDRTrunk | DSheirer/sdrtrunk | 2,223 | GPL-3.0 | 2026-11-06 | 活跃；0.6.1 正式 + 0.6.2 nightly；全应用形态 |
| B9b | OP25（boatbod） | boatbod/op25 | 493 | 无 LICENSE（osmocom 谱系） | 2026-11-01 | 活跃；GNU Radio 流水线形态 |
| B10 | 新兴：more1090 | daniestevez/more1090 | 39 | Apache-2.0 OR MIT | 2026-10-26 | Rust 重写 ADS-B，SIMD，宣称低 SNR/碰撞场景优于 readsb |
| B10 | 新兴：FreeDV RADE | freedv-gui + codec2 | 331/468 | GPL/LGPL 分组件 | 2026-11-06 | AI 声码器正式化（详见 §3） |
| B10 | 新兴：nrsc5 | theori-io/nrsc5 | 972 | MIT | 2026-10-17 | HD Radio 解码复活（新解码器 + HERE Images 流） |
| B10 | 新兴：wmbusmeters | wmbusmeters/wmbusmeters | 1,358 | GPL-3.0 | 2026-11-06 | wM-BUS 表计（rtl_433 系近亲） |
| B10 | 新兴：ka9q-radio | ka9q/ka9q-radio | 354 | GPL-3.0 | 2026-11-02 | 组播多信道（详见 §2） |

**架构共同点（七条范式）**：
1. **幅度域批处理是高速解码第一定律**——dump1090/readsb 把 I/Q 成批转 12-bit 幅度表后全程只扫整数，
   单 dongle 万包/秒；配套双线程解耦（采样线程 → 环形缓冲 → 解调线程批量消费）。
2. **后端抽象 = 一个头文件 + 每硬件一个实现文件**（dump1090-fa `sdr_rtlsdr/bladerf/hackrf/soapy/ifile/stub`）；
   `ifile`（文件回放）与 `stub`（无硬件）必然在列——可回放性是调试体验碾压闭源的根源。
3. **解码器是自描述注册项**：rtl_433 的 `r_device`（调制枚举、符号宽度、reset/gap/sync 阈值、tolerance、
   decode_fn、输出字段表、优先级、启停级别、统计计数）+ `-X` flex 声明式解码；SatDump 57 个目录级插件。
4. **分层流水线 + 层间可回放**：SatDump 显式 baseband→demod→frames→soft symbols→products 五级，任意级落盘。
5. **解码器是消息生产者，UI 只是消费者**：统一结构化消息 + 多出口（SBS/Beast/RAW、json/mqtt/influx、RTP 组播）。
6. **统计与自适应内建**：decode_ok/fails 计数、增益/阈值自适应（dump1090 adaptive.c）、CRC 多候选纠错（readsb）。
7. **许可两极**：解码器清一色 GPL-2/3；对外库宽松（LGPL/BSD/MIT）；遗留项目缺 LICENSE 是主要污点。

---

## 6. 移动端域

| 项目 | 2026 现状 | 要点 |
|------|-----------|------|
| **RF Analyzer 2.0** | 2025 大改版后活跃（demantz/RFAnalyzer） | **开源 Android SDR 首选**：F-Droid + Play 双渠道；HackRF/RTL-SDR/Airspy USB；实时 FFT+瀑布 |
| SDR Touch | 闭源（商业） | 本域用户基线，社区推荐正转向 RF Analyzer |
| SDRangel Mobile | 跟随 SDRangel 主线 | SDRangel 的 Android 形态 |
| rtl_tcp + Web 客户端 | 通用模式 | 手机只当显示器/遥控器——与本仓 PWA 思路一致 |

**与本仓关联**：本仓移动端主路是 E1′ PWA（WO-273 裁决），不重走原生 App——RF Analyzer 恰好证明了
「USB 直连原生 App」与「网络遥控 Web 客户端」是两个不相交的生态位，PWA 路线与它们不冲突。

---

## 7. 交叉趋势（六域合并观察）

1. **电台网络遥控赛道升温**（rigplane 3.0、Rigflow、SDRoxide、VLSC）——本仓窄赛道的窗口期收窄，
   支持把 WO-274 sidecar 集成与 PWA 主路尽快做到「可日常使用」而非 spike。
2. **AI 入链**：声码器（RADE）已正式化，神经接收机（Sionna）在研究侧成熟；应用侧整合器（OnAir、RTL-ML）出现。
   对本仓：数字语音远期接 FreeDV；近期无需追。
3. **框架换代但未交接**：GR4 到 RC3，生产仍压在 GR3.x + SoapySDR 上；Rust 侧 FutureSDR 尚不可押注。
   自研小而正确的 dsp（本仓路线）在此期间没有劣势。
4. **解码器范式收敛**（§5 七条）——这是全调研对本仓**可立即搬运**的密度最高的部分（见 §8）。
5. **托管平台漂移要警惕**：SatNOGS 真身在 GitLab（GitHub 是 2018 镜像）、osmocom 在自建 Gitea、
   acarsdec 归档——接手任何上游前先确认活跃仓库在哪。
6. **许可卫生是二次实现的隐形税**：Airspy/Thetis/OP25/Hermes-Lite/acarsdec 均缺仓库内 LICENSE，
   依赖它们的派生工作全靠谱系推定；本仓保持 AGPL + 依赖清单明示（已在 CLAUDE/交接文档执行）。

---

## 8. 对 radio-suite 的落地建议（按优先级）

1. **幅度域批处理进 dsp 共享路径**（readsb/dump1090 范式）：让同一份 I/Q→幅度缓冲成为瀑布、APRS AFSK、
   SSTV 三者的共同输入；解调按块扫整数幅度。改 `libs/dsp`（waterfall_frame.cpp）+ `aprs/afsk.cpp`。
2. **AX.25 FCS 1–2 位纠错候选**（readsb CRC 多候选思路）：弱信号 APRS 通过率显著提升，改 `aprs.cpp`。
3. **解码/解调链加回放与统计**：`ifile` 等价物（录 IQ/音频→离线回放）+ decode_ok/fails 计数（rtl_433 `-M` 思路）；
   igate.cpp 获得阈值自调。联调不再依赖电磁环境。
4. **SSTV / Voice Memory 拆三级 pipeline**（SatDump 范式）：baseband（录音）→frames（VIS 识别后）→products
   （成品图像/转写），每级可落盘回放；SSTV 模式定义改自描述结构体（VIS 码、行/脉冲时序、扫描参数）。
5. **次级解码（RTTY/POCSAG/DTMF/CW）直接上注册表模式**：`r_device` 式结构体声明字段表驱动 UI 列生成，
   priority/disabled 做并发仲裁；简单协议用声明而非代码。
6. **rigplane sidecar 切 3.0 正式版**（WO-274 收尾）：确认 3.0 对 Icom 网络 API 的覆盖与本仓用法一致后升级。
7. **远期**：FreeDV/RADE 数字语音（audio 通道已具备形态条件）；若拆 dsp 库单发，学 bladeRF 分组件 COPYING。

## 9. 数据来源

- 子代理 GitHub/GitLab/osmocom Gitea API 直查（2026-11-07）：各仓 ★、许可、push 时间的原始依据
- 桌面域：[sdrpp.org](https://www.sdrpp.org)、[AlexandreRouma/SDRPlusPlus](https://github.com/AlexandreRouma/SDRPlusPlus)、
  [sannysanoff/SDRPlusPlusBrown](https://github.com/sannysanoff/SDRPlusPlusBrown)
- Web 域：[tomaszdd/openwebrxplus-sdr](https://github.com/tomaszdd/openwebrxplus-sdr)、
  [fms.komkon.org/OWRX](https://fms.komkon.org/OWRX)、[ka9q/ka9q-radio](https://github.com/ka9q/ka9q-radio)、
  [sdrutah.org ka9q+WebSDR 指南](https://www.sdrutah.org/info/using_ka9q_radio_with_websdr.html)
- 框架域：[gnuradio.org news](https://www.gnuradio.org)、[fair-acc/gnuradio4](https://github.com/fair-acc/gnuradio4)
- 硬件域：[greatscottgadgets/hackrf](https://github.com/greatscottgadgets/hackrf)、
  [hackrf.readthedocs.io](https://hackrf.readthedocs.io)、[HackRF Pro](https://greatscottgadgets.com/hackrf/pro)、
  [myriadrf/LimeSuiteNG](https://github.com/myriadrf/LimeSuiteNG)、[hermeslite.com](http://hermeslite.com)、
  [airspy.com/download](https://airspy.com/download)
- 解码域：[merbanan/rtl_433](https://github.com/merbanan/rtl_433)、
  [wiedehopf/readsb](https://github.com/wiedehopf/readsb)、[flightaware/dump1090](https://github.com/flightaware/dump1090)、
  [SatDump/SatDump](https://github.com/SatDump/SatDump)、[satdump.org（Towards 2.0.0）](https://www.satdump.org)、
  [daniestevez/gr-satellites](https://github.com/daniestevez/gr-satellites)、
  [DSheirer/sdrtrunk](https://github.com/DSheirer/sdrtrunk)、[boatbod/op25](https://github.com/boatbod/op25)、
  [daniestevez/more1090](https://github.com/daniestevez/more1090)、[theori-io/nrsc5](https://github.com/theori-io/nrsc5)、
  [Theori blog](https://blog.theori.io/receiving-nrsc-5)、[freedv.org/radio-autoencoder](https://freedv.org/radio-autoencoder)、
  [ARDC：FreeDV RADE](https://www.ardc.net/freedv-rade-a-new-approach-to-digital-voice-over-hf-now-integrated-into-popular-radios)
- 移动域：[demantz/RFAnalyzer](https://github.com/demantz/RFAnalyzer)、
  [F-Droid RF Analyzer](https://f-droid.org/en/packages/com.mantz_it.rfanalyzer)、
  [r/RTLSDR RF Analyzer 2.0 发布贴](https://www.reddit.com/r/RTLSDR/comments/1mvm6nr/rf_analyzer_20_is_live_modern_android_sdr_app)
