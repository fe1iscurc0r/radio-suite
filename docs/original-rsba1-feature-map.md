# 原厂 RS-BA1 RemoteController 功能对照表（IC-705）

- 来源：`D:/my git/RS-BA1/RemoteController/models/IC-705.ini`（原厂定义文件，1004 行，
  本仓外素材）+ `vendor/rsba1-core/src/rsba1/radio_link.py`（命令码真机验证）
- 用途：radio-suite 的功能完整性以此为准绳，逐项对齐；本表是 M1–M3 的验收清单。
- 日期：2026-10-24

## 状态图例

✅ 已实现（含 mock 语义与测试）｜🚧 协议层已有码、UI 待接｜📋 列入里程碑｜🅿️ P3+ 另议

> 真机线路自 2026-08 起按指示搁置：✅ 只代表**协议帧 + UI 已由 mock 与帧级测试
> 锁定**，不代表真机端到端已验。2026-10-27 新增的四个原厂件的真机复验清单见 §K。

## A. 核心操作

| 原厂项 (ini 注释) | CI-V | 状态 | 说明 |
|---|---|---|---|
| Tx (PTT) | 1C 00 | ✅ | 二次确认 + 10min 自动回 RX + 非业余段拦截 |
| XFC | — | 📋 M3 | 发射频率监听 |
| Split | 0F 00/01 | 🚧 | 写已接 + 发射中调谐保护；无回读（拆分状态不回读） |
| Freq | 05/03 | ✅ | 手动输频 + 步进 + 频段/波段快捷 |
| Mode | 06/04 | ✅ | LSB/USB/AM/CW/FM/WFM 按钮组 |
| Fil（滤波器） | — | 🚧 | 模式回读含 filter 号；选择器 M3（Filw/Slope/Roof 同期） |
| Dup / Dupofs | 0F/0D | ✅/🚧 | 方向循环已接；频差偏移值可回读并在 TX 行显示，设置 UI M3 |
| RIT / RIT频率 | 21 01/02 | 🚧 | RIT/ΔTX 开关写 + 回读已接；RIT 频率步进值 M3 |
| Dial Lock | — | 📋 M3 | |
| VFO（A/B/等量/交换） | 07/B0/A0 | ✅ | 原厂 ini 标 0C，实机为 07 选 VFO、B0 交换、A0 等量（§K.2） |
| Tracking | — | 📋 M3 | |

## B. 亚音 / 编码

| Tone/Tsql/Dtcs + 号码 + Dsq/Csqlno | 16 5D/1B… | 📋 M3 | 中继亚音全套（协议码在 radio_link 562-612 已验） |

## C. 前级 / 增益

| 原厂项 | 码 | 状态 |
|---|---|---|
| Pre（PAMP P1/P2） | 16 02 | ✅ 循环按钮 + 回读 |
| ExtPre | — | 📋（外部选件） |
| Att | 11 | ✅ 仅 HF/50MHz 可设（真机 NAK 语义已还原） |
| IP+ | — | 📋 M3 |
| Agcstat/Agcval | 16 12 | ✅ 档位循环；深度滑杆 M3 |
| Rfg | — | 📋 M3（RF 增益） |

## D. 降噪 / 滤波 / 音频

| 原厂项 | 码 | 状态 |
|---|---|---|
| Nb | 16 22 | ✅ |
| Nr | 16 40 | ✅（深度 Nrd M3） |
| Atnotch（自动陷波） | 16 41 | ✅ |
| Mnnotch（手动陷波） | 16 48 | 🚧 码已有，UI M3 |
| Moni | 16 45 | ✅ |
| Vox | 16 46 | ✅（延迟/增益参数 M3） |
| Comp | — | 📋 M3 |
| Apf / Tpf | — | 📋 M3 |
| CW 侧音/键速/PBT/Bkin | 17 / 14 09 0C / 16 47 | ✅/📋 | 文本发送、侧音、键速、Break-in 已接（§K.1）；PBT 📋 |

## E. 发射限制

| Txp / MaxTxPower(电池/DC 双限) / EMR / BK | — | 📋 M3 | 功率上限属安全域，进 SafetyGate 联动 |

## F. 频谱（Scope 全家 14 项）

| Scope Span/Edge/Speed/Att/RefLv/Dual/… | — | 📋 M2 | 本仓瀑布为强项，M2 对齐原厂 Span/Edge 语义 |

## G. 仪表（8 种）

| Met_Sql/S/Ovf/Po/Swr/Alc/Comp/Id/Vd | — | 📋 M3 | CI-V 可回读 Po/Swr/Alc；表头切换 UI |

## H. DV 数字语音 / 记忆 / 扫描

| Mycall/Txmsg/DV 收发呼叫与录音 | — | 🅿️ | DV(D-STAR) P3+ 另议 |
| Mseq / Memory 通道 | 1A 00 / 08 | ✅/📋 | 信道读/写 + 选组/选信道已接（§K.4）；Mseq 记忆序列 📋 |
| Scan | — | 📋 M3 | |

## I. 接收范围（原厂权威数据，已入代码）

- [FREQUENCY] RANGE0 = 30kHz–199,999,999；RANGE1 = 400–470MHz（**IC-705 无 1.2G 段**，
  早期版本我写的 1.45GHz 上界是错的，已修正，`core/bands.cpp`）
- 接收波段快捷：AIR 118–137（中点 127.5，AM）/ BC-FM 87.5–108（中点 97.75，WFM）/
  BC-SW 31m（9.5，AM）——**波段区间而非点频**，▲▼ 在波段内连续可调；发射仍锁业余白名单。

## J. 横向集成（师傅点名项）

| 项 | 规约 |
|---|---|
| rsba1 UDP 真机链（"机器码"） | **M1 主体**：0x80 登录/发现/Serial+Audio 信道/重传重排；参考 vendor/rsba1-core（MIT）+ APRSLocus icom_lan_protocol.dart 交叉对照 |
| hamlog（scratchpad） | M4：ADIF 文件交换（工单明确不做实时同步） |
| lumo/scratchpad 嵌入 | M5：`--headless --bus` 本机总线，宿主只作消费者 |
| APRS | **P3 另议**（工单 §五"不做 APRS 发射"）；接收/显示侧若提前立项：APRSLocus（GPL-3.0）依 AGPL §13 并入，数据（tocalls=CC BY-SA 2.0 / 符号图）单独核许可 |
| scratchpad（AGPL 同许可） | 组件可双向流动（如 SpectrumPanel），PR 须标注来源/commit/版权人；当前 GLSL 瀑布为自研，等 M2 重写时评估是否换用 |

## K. 2026-10-27 补齐的四个原厂件（mock 帧级验证，真机未验）

按 `libs/protocol/rsba1-core/src/rsba1/radio_link.py` 与 rigplane
`main @ 134f8446`（`rigs/ic705.toml` + `commands/*.py`）双源核对后实现，全部走
`RadioSession` 单一出口；UI 为主窗口 VFO 按钮 + 三个对话框。**真机端到端未验**
（线路搁置），下表同时就是真机复验清单。

| 件 | CI-V | 落地位置 | 测试 |
|---|---|---|---|
| CW Keyer | 0x17 文本 / 0xFF 停止；0x14 0x09 侧音、0x14 0x0C 键速、0x16 0x47 Break-in | `app/cw_keyer.*`、`session sendCwText/stopCw` | `[civ] cw …`、`[mock] cw …`、`[session] cw …`、`[ui] cw keyer dialog …` |
| 双 VFO（SUB 带） | 0x25 sub 频率、0x26 sub 模式、0x07 选 VFO、0xB0 交换、0xA0 等量 | 主窗口 SUB 行 + `A/B/A⇄B/A=B` | `[civ] dual-vfo …`、`[session] sub band …` |
| Set Mode 深设置树 | 0x1A 0x05 group item [value…] | `app/set_mode_dialog.*`（14 个常用项预设 + 任意 group/item） | `[civ] menu item …`、`[ui] set mode dialog …` |
| Memory Channel | 0x1A 0x00 读/写 26 字节块；0x08 选组 / 选信道 | `app/memory_channel.*` | `[civ] memory channel …`、`[session] menu and memory …`、`[ui] memory channel dialog …` |

真机复验要点（按风险排序）：

1. **PTT 状态读回**（0x1C 0x00）——旧代码写作 0x14 0x0C，与 CW 键速电平冲突，
   已纠正；真机确认读/写均走 0x1C 0x00。
2. **键速/侧音量化的边界**：WPM 6–48 ↔ 0–255 用 nearest-half-down，
   侧音 300–900 Hz ↔ 0–255 编码向上取整、解码 5 Hz 量子；复核首尾与中点。
3. **Break-in 子命令 0x16 0x47** 在 IC-705 上的可用性（参考实现有码，本机未验）。
4. **记忆信道 26 字节块偏移**：本仓按 payload 含 subcmd 解析，已与
   `parse_memory_contents_response` 逐字段对齐；用真机信道 1 做一次读→写→读。
5. **0x25/0x26 的 sub 语义**：线上 0 = 当前选中 VFO；本仓按本地记录的
   `selected_sub` 折算 A/MAIN 与 B/SUB（0x07 只写，无法回读核对，需真机人工确认）。
6. **菜单项 0x1A 0x05 的 group/item 取值表**（IC-705 手册 A7560-8EX-1 附录），
   对话框里的 14 个预设只是常用项，真机需抽查 3–5 项读写。

## 本仓与原厂的差距声明（诚实版）

原厂 = 上述 ~120 个可控项 + 成熟面板布局。本仓当前 = A/C/D 三区约 20 项
+ Set Mode 任意菜单项读写 + 记忆信道 + 瀑布/门禁/测试底座。Set Mode 与 Memory
把「逐项重画 UI」变成「按菜单表读取」，其余差距按 M1(真机链) → M2(频谱对齐) →
M3(面板补全) 收敛，本表是逐项验收单。
