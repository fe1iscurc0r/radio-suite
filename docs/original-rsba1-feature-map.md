# 原厂 RS-BA1 RemoteController 功能对照表（IC-705）

- 来源：`D:/my git/RS-BA1/RemoteController/models/IC-705.ini`（原厂定义文件，1004 行，
  本仓外素材）+ `vendor/rsba1-core/src/rsba1/radio_link.py`（命令码真机验证）
- 用途：radio-suite 的功能完整性以此为准绳，逐项对齐；本表是 M1–M3 的验收清单。
- 日期：2026-10-24

## 状态图例

✅ 已实现（含 mock 语义与测试）｜🚧 协议层已有码、UI 待接｜📋 列入里程碑｜🅿️ P3+ 另议

> 真机线路自 2026-08 起按指示搁置：✅ 只代表**协议帧 + UI 已由 mock 与帧级测试
> 锁定**，不代表真机端到端已验。2026-10-27 新增的四个原厂件的真机复验清单见 §K，
> UI 重排 / 中继双频 / 亚音一族的验证口径与真机清单见 §L。

## A. 核心操作

| 原厂项 (ini 注释) | CI-V | 状态 | 说明 |
|---|---|---|---|
| Tx (PTT) | 1C 00 | ✅ | 二次确认 + 10min 自动回 RX + 非业余段拦截 |
| XFC | — | 📋 M3 | 发射频率监听 |
| Split | 0F 00/01 | 🚧 | 写已接 + 发射中调谐保护；无回读（拆分状态不回读） |
| Freq | 05/03 | ✅ | 手动输频 + 步进 + 频段/波段快捷 |
| Mode | 06/04 | ✅ | LSB/USB/AM/CW/FM/WFM 按钮组 |
| Fil（滤波器） | — | 🚧 | 模式回读含 filter 号；选择器 M3（Filw/Slope/Roof 同期） |
| Dup / Dupofs | 0F/0D | ✅ | 方向 + 频差（100 Hz BCD，≤9.9999 MHz）可写可回读；LCD 与中继对话框同时显示 RX/TX 两个频率（§L.2） |
| RIT / RIT频率 | 21 01/02 | 🚧 | RIT/ΔTX 开关写 + 回读已接；RIT 频率步进值 M3 |
| Dial Lock | — | 📋 M3 | |
| VFO（A/B/等量/交换） | 07/B0/A0 | ✅ | 原厂 ini 标 0C，实机为 07 选 VFO、B0 交换、A0 等量（§K.2） |
| Tracking | — | 📋 M3 | |

## B. 亚音 / 编码

| Tone/Tsql/Dtcs + 号码 + Dsq/Csqlno | 16 5D/42/43/4B + 1B 00/01/02 | ✅ | 模拟亚音（CTCSS/TSQL）+ 数字亚音（DTCS）全套：模式、收发频率、码值、极性；EIA 50 音 / 104 个常用 DCS 码（§L.3，真机未验） |

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
| 网络音频（RS-BA1 50003） | UDP 50003 | ✅ | RX 流式播放 + PTT 门禁麦克风上行；应用内「网络音频」开关，开启后频谱改用真实 RX PCM（**真机未验**，§L.6） |

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

## L. 2026-10-27 UI 重排 + 中继/亚音（mock + 离屏 UI 验证，真机未验）

用户点名项：按钮尺寸与观感、频率调法、双守听、频差双频显示、模拟亚音 / 数字亚音。
全部走 `RadioSession` 单一出口，UI 只发命令、只读回状态。

### L.1 主窗口布局（`app/MainWindow.*`）

- 第一行仪表/调谐同排：S 表靠左（240×108，限宽 320）、132×132 `DialWidget`
  居中（拖拽按角度、滚轮按格）、Po/SWR 缩成右侧小表（180×64，两条短横条）；
  四档步进按钮 + 当前步进文案，▼/▲ 与输频/GO 同一行。
- LCD 顶栏：RX 主频 44pt、模式 17pt、TX 副频 18pt，收发双频一眼可读。
- 按钮统一圆角 6px 与 hover / pressed / checked 三态；PTT 220×84 / 15pt，
  连接 88×40，模式键 56×34，功能键 56×32；窗口最小 1180×860。
- 波段行：7 个业余段 + AIR / BC-FM / BC-SW 预设（54×28 / 64×28）；双 VFO 行：
  SUB 频率（等宽青色）+ `A 主 / B 副 / A⇄B / A=B`；功能区分两行。
- 频谱/瀑布最小高 260 且纵向拉伸，改排后占主列大部分高度；最近 QSO 列表与
  HamLog 工作台入口同排，不再单独吃掉一条高度。

### L.2 双守听：IC-705 不支持（诚实说明）

- 原厂 `IC-705.ini` 顶部即 `DUAL = 0`、`DUAL_KIND = 0`；CI-V `0x07` 只有
  MAIN/SUB 选择，没有双接收控制；rigplane `ic705.toml` 也是单接收机模型。
- **不做假 DUAL 开关**。主窗口「双守听？」按钮只弹说明，替代方案 = 现有
  A/B 双 VFO（SUB 带）手动切听：`0x25/0x26` 管 SUB，`0x07` 切换，`0xB0` 交换。
- 中继/亚音对话框 `app/repeater_dialog.*`：RX / TX 两行同时显示（DUP− 时
  TX = RX − offset，DUP+ 时 RX + offset，simplex 时同频）；方向 `0x0F`、
  频差 `0x0D`、CTCSS/TSQL 模式与收发频率、DTCS 码与收/发极性，底部原始回读行。

### L.3 亚音协议（`core/civ.*`）

| 功能 | CI-V | 载荷 |
|---|---|---|
| 亚音功能模式 | `16 5D` | 00 OFF / 01 TONE / 02 TSQL / 03 DTCS / 06 DTCS(T) / 07 TONE(T)+DTCS(R) / 08 DTCS(T)+TSQL(R) / 09 TONE(T)+TSQL(R) |
| 中继音开关（CTCSS 发射） | `16 42` | 00/01 |
| TSQL 静噪开关 | `16 43` | 00/01 |
| DTCS 开关 | `16 4B` | 00/01 |
| 中继音频率 | `1B 00` | `[sub][00][100Hz\|10Hz][1Hz\|0.1Hz]`，0.1 Hz 单位 |
| TSQL 频率 | `1B 01` | 同上 |
| DTCS 码 + 极性 | `1B 02` | `[sub][TX 极性<<4 \| RX 极性][百位][十位<<4 \| 个位]`，极性 0=Normal / 1=Reverse |

- 88.5 Hz → `00 08 85`（deci-Hz 885）；回读要求 `payload[1] == 0x00`，否则视为非亚音频应答。
- DTCS 码三位八进制（023–754）；极性字节的高低半字节归属按指南 p.21 格式推定，
  **真机未验**（见 L.5）。
- 下拉表：EIA 50 个标准 CTCSS 音（670–2541 deci-Hz）、104 个常用 DCS 码。

### L.4 测试（全量 98/98）

`[civ] offset frames use 100 Hz BCD`、`[civ] ctcss and dtcs frames match guide layout`、
`[session] offset roundtrip and tx frequency`、`[session] ctcss and dtcs settings roundtrip`、
`[ui] repeater dialog shows both frequencies and applies tone settings`；
离屏 UI 测试真点 `rep_dir / rep_offset / rep_tone_mode / rep_tone_freq / rep_dtcs_*`
按钮，断言 mock 帧与状态回读。

### L.5 真机复验要点（新增，按风险排序）

1. **0x1B 0x00/0x01 的 0.1 Hz BCD**：88.5 Hz → `00 08 85`；复核 254.1 Hz
   （高位不超过 2）与 67.0 Hz 两个边界。
2. **0x1B 0x02 极性字节**：用 023 N/N、023 R/N、023 N/R 三次写→读，
   确认 TX 在高半字节、RX 在低半字节，还是相反。
3. **0x16 0x5D 组合值**：0x07 / 0x08 / 0x09 是否被 IC-705 接受（不接受时 NAK 语义）。
4. **0x0D 频差**：100 Hz 步进与 9.9999 MHz 上限；DUP± 后 LCD TX 行是否与真机面板一致。
5. **0x0F 读回归一**：本仓把 simplex 归一为 0x00，真机 2026-08-24 实测可能仍回 0x10，
   需确认后决定是否保留归一。

### L.6 50003 音频真机复验清单（新增）

代码已接、门禁有单测（`AudioBridge` 半双工/有界队列/失败重试 6 例），
但**未在 IC-705 上实际通话**。按风险排序：

1. **RX 播放**：`--radio` 登录成功 → 点「网络音频」→ 应听到 50003 解出的音频；
   目标 48 kHz / s16le / 单声道 / 20 ms 帧（960 样本）。若无声，先看按钮旁状态行
   是否报「50003 音频信道未握手」。
2. **音频是否反相/错序**：两包（`0x6c 0x05` / `0x44 0x02`）拼接后的 PCM 应是
   连续 20 ms；噪声/爆音多为 seq 起始值不对。
3. **PTT 门禁麦克风上行**：按住 PTT → 电台应发射且有声；松开即停。TX 期间本地
   RX 播放应静音（半双工），队列在相位切换时清空。
4. **真实频谱**：网络音频开启且收到 PCM 后，瀑布/谱线应来自电台 RF 而不是合成
   信号；对比关闭开关后的合成谱形态。
5. **设备选择**（2026-10-30 已补）：主窗「音频设置」列 WASAPI 输入/输出（ID 非
   连续，按 `getDeviceIds()` 取真 ID，不当下标用），选 `audio/in_dev`、
   `audio/out_dev`；-1 = 跟随系统默认。开启中改设备会 stop→start 重开，失败则按钮
   回弹并在状态栏报原因，避免"显示 ON 但没声"。
6. **质量计数**（2026-10-30 已上界面）：「音频设置」每 0.5 s 刷新 RX 块/重排待发/
   判丢/迟到丢弃、TX 帧/门禁丢弃、扬声器积压/欠载/丢弃、麦克风溢出。真机复验
   以丢=0、欠载=0 为通过线；有数但持续增长时先看是 50003 网络抖动还是本机设备。

## 本仓与原厂的差距声明（诚实版）

原厂 = 上述 ~120 个可控项 + 成熟面板布局。本仓当前 = A/C/D 三区约 20 项
+ Set Mode 任意菜单项读写 + 记忆信道 + 瀑布/门禁/测试底座。Set Mode 与 Memory
把「逐项重画 UI」变成「按菜单表读取」，其余差距按 M1(真机链) → M2(频谱对齐) →
M3(面板补全) 收敛，本表是逐项验收单。
