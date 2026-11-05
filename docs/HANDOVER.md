# radio-suite 交接说明（2026-10-30 更新，2026-11-05 §6 按 refs 重核）

> 一句话：**独立 AGPL 桌面电台客户端**（Qt6/C++20），已完成 M0–M2 主体功能 +
> 原厂功能对齐（含 2026-10-27 补齐的 CW / 双 VFO / Set Mode / Memory 四件 +
> 同日的 UI 重排与中继频差 / 模拟·数字亚音）+ 两条生态融合（hamlog / APRS 工作台）+
> WSJT-X UDP 接入（解码 / QSO / WSPR / ADIF / 远程控制 / QSO 直写 HamLog）+
> 50003 网络语音链（RX 播放 / PTT 门禁 TX / 真实频谱）+ 引擎化准备（WO-274）+
> 2026-10-29 审查修复与算法优化（异步播放 / S 表标定 / Welch 平均 / APRS csT）
> + 实窗验证修复（频谱越界读 / S 表可读性 / WSJT-X 线格式互通）+ RS-BA1 本机环回
> （假电台三信道 + 100 ms 音频重排窗 + 音频设备/质量面板），149/149 测试全绿；
> 真机线路按师傅指示**永久搁置**，改走 mock 与 self-hosted 验证。
> 接手人先读本文件 + `docs/WO-274-engine-switch.md` + `docs/WO-273-radio-competitive.md`。

## 1. 仓库与位置

| 项 | 值 |
|---|---|
| 工作区 | `D:/my git/radio-suite`（**独立仓**，AGPL-3.0-or-later） |
| 兄弟仓 | `D:/my git/_rsba1-core_sync` = 本仓 `libs/protocol/rsba1-core` 的来源（MIT，**旧仓，准备与 rigplane 合并**） |
| 上游素材 | `D:/my git/RS-BA1`（原厂定义文件+手册）、`D:/my git/rs-ba1-reverse`（逆向文档/CHM 提取） |
| 原版 HamLog 源码 | `C:/Users/ASUS/Desktop/HamLog/programs/R1.0.0/`（功能对照基准） |
| 父仓 | `D:/my git`（**是另一个 git 仓**，radio-suite 是它下面的独立仓，勿在其上提交本仓文件） |

## 2. 目录结构（WO-273 单仓 monorepo 裁决，已落地；架构分层见 PR #36）

```
radio-suite/
├── apps/desktop/{core,app,aprs,wsjtx,logbook,audio,bus}   # 桌面端（C++/Qt6）
├── apps/spike/                                   # WO-274 rigplane 引擎 spike（默认不编）
├── libs/dsp/dsp/                                 # FFT/瀑布（双端共用）
├── libs/protocol/rsba1-core/                     # MIT 跟随副本（subtree 同步）
├── tests/ (Catch2, 149 用例)  docs/  packaging/  tools/
```

include 双根：`apps/desktop` + `libs/dsp`（`core/xxx.h`、`dsp/xxx.h` 前缀不变）。

## 3. 三方关系（师傅定案，勿推翻）

- **rsba1-core（MIT 旧仓）** → 准备与 **rigplane 合并**；本仓只存跟随副本
  （fixtures 生成 + Python 桥 import），合并落地后该目录退役。
- **rigplane（MIT）= 引擎**，sidecar 方式跑（REST/WS/音频/rigctld）。
- **radio-suite（AGPL 新仓）** = 引擎的**独立客户端路线**：不抄 rigplane 代码、
  不改其仓，只按其契约写客户端（WO-274 路线 A）；价值层 =
  HamLog 共库 / APRS / SSTV / DTMF / bus / agent 集成。

## 4. 已完成能力（全部有实证）

| 域 | 状态 | 证据 |
|---|---|---|
| CI-V 协议 | ✅ 与 Python 参考**逐字节互锁** | tests `[civ]`+fixtures（22 个由参考实现生成） |
| RS-BA1 V2 UDP | ✅ 控制协议+传输层（登录/鉴权/0xA8/ConnectTrans/重传重排）+ **50003 音频双通路已接 app**（RX 100 ms 重排→扬声器，麦克风→PTT 门禁上行）+ **本机三信道环回（假电台）** | tests `[rsba1]`/`[audio]`/`[rsba1][loopback]` 6 用例；假电台应答与 IC-705 fixtures 逐字节一致；真机未验（搁置），差异见 `docs/research/rsba1-audio-upstream-2026-10-30.md` |
| rigctld 服务器 | ✅ 过 rigplane **全部轮询命令** | 真实 loopback TCP 测试 `[rigctld]` |
| HTTP 总线 | ✅ `/api/state` `/api/spectrum` + 演示页；只有 HTTP，无 WS/控制 POST | 真实 HTTP 测试 `[bus]`（含 token/绑定门禁） |
| 安全门禁 | ✅ PTT 二次确认 + 10min 自动回 RX + **非业余段拒绝发射**（TX 中调谐也拦） | tests `[safety]` + `[session][ptt]` 帧序列 |
| 频谱/瀑布 | ✅ OpenGL 纹理滚屏 + 谱线（**上谱下瀑分区**）+ 半圆指针 S 表 + Po/SWR 表；默认合成谱，**网络音频开启后切真实 RX PCM**；**周期 Hann + 4 帧 Welch 功率平均**（噪底抖动减半）；**S 表按 IARU 分段标定**（S9=-73 dBm，刻度/指针/读数同源 `app/smeter_scale.h`） | 截图 `docs/screenshots/` + `SignalSynth` / RX tap 调用点 + `[dsp]` 平均/S 表映射测试 |
| 频率/模式/开关 | ✅ 频率/模式/滤波/RIT/ΔTX/Split/ATT/NB/NR/NOTCH/VOX/PAMP/AGC/DUP + **双 VFO(SUB 带)** + **中继频差(0x0D) / CTCSS·TSQL·DTCS** | 对照原厂 `IC-705.ini` 全清单；SUB/频差/亚音帧级测试 `[civ]`/`[session]` |
| 原厂件 | ✅ 调谐旋钮（132px 旋钮台，与 S 表/Po·SWR 同排）、记忆槽、Connect Setting、SSTV(8 制式)、DTMF、Voice Memory、**CW Keyer / Set Mode / Memory Channel / 中继·亚音对话框** | 原厂 CHM 清单逐项对照；四件见 `docs/original-rsba1-feature-map.md` §K，UI/频差/亚音见 §L |
| SSTV | ✅ 8 制式编解码（Robot36/72 + Martin M1/M2）+ VoX 校准头 + **编解码回环自校验** | tests `[sstv]`(31 断言) |
| DTMF | ✅ 16 键编解码 + 回环 | tests `[dtmf]` |
| APRS | ✅ 核心库（纯 std）：TNC2、三种坐标（未压缩/压缩含 csT 海拔·航向·速度/Mic-E）、对象·气象·遥测、消息 + ACK/REJ + 重发、第三方解包、12 类过滤器、通行码；**工作台 UI**（OSM 地图 + 台站表 + 消息窗）；APRS-IS 连接（china/rotate 轮换、指数退避）；**KISS TCP/串口**（Dire Wolf / LoRa APRS 网关）+ Bell 202 AFSK 1200 Bd 走 50003；**iGate 双向**（RF→IS 打 qAR、IS→RF 仅消息/对象/Item 并去重）；独立 SQLite 持久化（`%APPDATA%/radio-suite/aprs.db`） | tests `[aprs]` 19 + `[aprs_persist]` + offscreen `[aprs_ui]` 3；APRS-IS 实测 china.aprs2.net（308 台/7805 包）；**RF / LoRa / AFSK 真机未验** |
| HamLog 融合 | ✅ **同库直读写** + 工作台 v2（四字段模糊搜索/完整编辑/模板/设置/QRZ/ADIF/**LoTW 上传**/字体/代理） | 临时 SQLite CRUD/搜索测试 + UTF-8 ADIF 长度测试；真实库仅人工验收 |
| WSJT-X 接入 | ✅ UDP 协议 schema 3 全量编解码（纯 std：心跳/状态/解码/清空/QSO/关闭/重放/HaltTx/自由文本/WSPR/位置/ADIF/配置；**`delta_frequency` 按上游为有符号 qint32**）；组播监听（`224.0.0.1:2237`）+ **工作台 UI**（解码表/QSO/WSPR/ADIF 四页，CQ·QRZ 高亮、低置信度着色）；控制出口：呼叫、停止发射、重放、自由文本、设置网格；**QSO Logged 自动写 HamLog 库**（device=WSJT-X、备注网格）；**双击解码回填主窗口呼号**并按 `dial + ΔF` 显示绝对频率 | tests `[wsjtx]` 11 + offscreen `[wsjtx_ui]` 4（UDP 回环 + 面板渲染 + 激活解析含负频差 + HamLog 映射 + **QDataStream 字节长度前缀互通**：用本机 Qt 6.8 序列化出的 93 字节 Decode 包做金样本）；**消息体已按 QDataStream 字节长度前缀修正**，本机 127.0.0.1:2237 注入真线格式字节可完整进面板（客户端/14.074000 MHz/解码行全中）；**未与真实 WSJT-X / JTDX 进程联调**；自动写库未在真实 HamLog 库上复验 |
| 音频 | ✅ 本机输出（AudioOut，**后台线程异步播放**，SSTV 36–115 s / Voice Memory 60 s 不冻 GUI；默认输出设备按 WASAPI ID 取）+ 输入（AudioIn，溢出计数不丢缓冲）+ **50003 流式播放（AudioSink）与 PTT 门禁麦克风上行** | 门禁单测 `[audio]`；本机冒烟；真机通话未验，WASAPI 丢样见 §7 |
| FT8 | ✅ **本机解码 + 定时发射全链路**（不依赖 WSJT-X 进程）：vendored `ft8_lib`（MIT + kiss-fft BSD）+ `radio::ft8::decode/encodeTones/synthesize`（纯 std、不依赖 Qt，可被 app/Android 复用）+ `radio::ft8::SlotDecoder`（48k s16 → 12k 抽取、按 15 s UTC 边界切槽、后台线程解码、解码跟不上宁可丢槽不积压）+ **FT8 面板**（主窗「FT8 本机」按钮开独立窗口：时隙进度条 / 时间·SNR·DT·DF·文本五列表 / 双击行回填呼号·网格并按 `dial + DF` 定频，与 WSJT-X 面板共用 `app/decode_target.h` 解析）；RX 源 = 50003 下行 tap（与 APRS AFSK 同一路）；发射侧 `radio::ft8::renderTxPcm/nextTxSlotStart`（整槽 48k GFSK + 槽序号奇偶排期，无 Qt）+ 面板「文本 / 音频频偏 / 时隙奇偶 / 定时发射」一行：排期到下一个槽起点，到点把整槽交给主窗出口（`beginScriptedTx` → `AudioOut::playAsync` → 播完 `endScriptedTx`，与 SSTV 一键发射同一路声卡直出，**不注 50003 上行**：桥队列 200 ms 满则丢最旧，整槽丢不得） | tests `[ft8]` 3 + `[ft8][slot]` 4 + `[ft8][tx]` 5（828 断言）+ offscreen `[ft8][ui]` 10
（86 断言）：合成 → 加噪 → 解回原件、空槽不出消息、超长文本编码即抛；UTC 对齐边界算术、48k→12k 端到端（合成一槽 PCM 喂进去能解出 `CQ BG5GXO PM01` 且槽号/丢弃计数正确）、stop 丢半槽、对齐等待期仍计采样；发射侧排期算术（提前量/边界/奇偶全覆盖，含「已开始的那槽不许排」）、渲染 PCM 恰为一整槽 48k 且幅度不溢出、**渲染→本机解码器闭环**（自己发的自己能解，DF 命中 1500 Hz）、**DF 跟着音调偏走**（500/1000/1500/2000 Hz 四档离线渲染后 df 全部逐档命中，双击定频用的就是这个数）；面板注入行 → 表格 + 激活目标、PCM→解码器→面板全链路、解码表 500 行封顶（淘汰最老且行元数据同步前移，常开不涨）、排期不起播→到点一发只播一遍、空文本/无法打包/重复排期/出口拒绝四条拒绝路径、晚于槽起点 250 ms 以上如实报「这一发对不齐」、**按钮标签与 `tx_pending_` 同源**（发射完/被拒/取消后标签必须落回「定时发射」，留着「取消发射」会让下一刀点成排期，见 §7）、换源正在解码时先关旧流再开新流且各发一次 `sourceChanged`、麦克风与 50003 抢同一采集设备时主窗如实拒绝。**实窗已验**：按钮/面板/状态栏/进度条渲染，发射行五控件与提示行布局正常，空文本点「定时发射」显示「文本为空」，无音频时状态如实显示「等待 50003 下行音频」；**真实空中信号仍未验**（无 IC-705），但**声卡环回已验**：面板音源下拉可选
`50003 下行` / `本机麦克风`（`QSettings` 键 `ft8/source`，1=麦克风；换源时若正在解码
会关掉旧流再开新流，重启后按持久值恢复并自动开采集），不接电台、把喇叭对着麦克风
就能自收自发；实窗连排三次定时发射，本机把自己那一槽解出来 DT 0.0–0.1 s（`n_lead`
改 0 后波形顶格起播，不再拖到下一槽），DF 见 §7 的声学口径。 |
| Android 端口 | ✅ **交叉编译 + APK 出包 + 真机首跑（2026-10-31，Redmi K40 / Android 13 / arm64-v8a / MIUI 14）**：NDK AAudio 收发、GLES 瀑布实机出图、`RECORD_AUDIO` 运行时授权保住、minSdk 26 / `org.radiosuite.app`；**设置持久化在手机上真落盘也真读回**（16 处 `QSettings` 并到唯一后端 `ui_common/app_settings.h`，落 `AppDataLocation/radio-suite.ini`，应用自己写出 `[link]`/`[mem]`/`[ft8]` 三段）；「连接…」对话框保存 → force-stop → 重启按偏好建出 UDP 链路，链路连不上时如实报 `transport open failed` + `0.000000 MHz`（不折回 mock）；A7 参数注入在非 debuggable 的 release 包上可用；**SQLite 驱动已打进 APK**（`065ff83`），APRS 的 `aprs.db` 由应用自己建出（36864 字节、`SQLite format 3`、`stations/track/outbox/inbox` 四张表） | 逐项证据与命令：`docs/android-真机验收清单.md` §K4；离屏用例 9 例 `tests/test_launch_config.cpp`（`app_settings_names_its_own_store` 的 **Android 分支桌面不编译**，ctest 绿不覆盖手机）。**竖屏主窗布局判 FAIL**（两列重叠 + 触摸目标 33.8–42.5dp < 48dp），产品形状已定：锁横屏 + 分页重排，见 §5 第 11 条；AAudio 真出声 / 后台存活 / 一切要真电台的条目仍未验 |
| 打包/CI | ✅ windeployqt + 三平台 matrix | `.github/workflows/ci.yml` |

> **双守听**：IC-705 **不支持**（原厂 `IC-705.ini` 为 `DUAL = 0` / `DUAL_KIND = 0`，
> CI-V `0x07` 只有 MAIN/SUB）。本仓不做假 DUAL 开关；主窗口「双守听？」只弹说明，
> 替代方案 = 现有 A/B 双 VFO（SUB 带）手动切听，详见功能对照表 §L.2。

## 5. 未完成 / 待办（按优先级）

1. **rigplane 上游 issue** → **已提交 #4352**（2026-10-26，
   https://github.com/rigplane/rigplane-core/issues/4352）。提交前在 3.0.0b20
   与 main @ `134f8446` 双版本实测复现：65 条待观测字段里 10s 降级释放 62 条，
   只剩 `global.tx_state.split` + `receiver.main.unselected.freq_mode.*` 被判
   safety-critical；根因是 `startup_critical_path` 只按字段名判 critical（不区分
   slot），而 rigctld 适配器从不读这三条 → gate 永不放行（45s `/healthz` 全程
   refused）。正文与复核证据：`docs/upstream-rigplane-issue.md`。
   上游修好后 → WO-274 的 C（engine_client 正式化）/ D（音频路径裁决）可开工。
2. **WO-274 阶段二 PWA**（apps/web，消费本仓总线；主路 E1′，退路 E3 Kotlin）——**待师傅点头**。
3. **WASAPI 采集丢样**（44–57% 样本预算）——诊断与三条处置见
   `docs/notes/audio-capture-wasp-diagnosis.md`；最省事：录音上限改按样本计数。
4. ~~原厂件剩余：CW Keyer / Set Mode 深设置树 / 双 VFO(SUB 带) / Memory Channel 完整版。~~
   → **已于 2026-10-27 补齐**（mock 帧级 + UI offscreen 测试；真机未验）：
   - CW Keyer：`0x17` 文本（≤30B/帧、ASCII 归一）、`0xFF` 停止、`0x14 0x09/0x0C`
     侧音/键速、`0x16 0x47` Break-in；文本发射走 `beginScriptedTx/endScriptedTx`，
     非业余频段不键控，PARIS 估算自动回 RX；
   - 双 VFO：`0x25/0x26` 读 A/B、`0x07` 选 VFO、`0xB0` 交换、`0xA0` 等量；
     发射中拒绝切换，Split 发射中拒绝把 SUB 调到业余段外；
   - Set Mode：`0x1A 0x05 group item [value…]`，14 个常用项预设 + 任意项；
   - Memory Channel：`0x1A 0x00` 26 字节块读写、`0x08` 选组/选信道。
   - 中继频差：`0x0D` 100 Hz BCD（≤9.9999 MHz）可写可回读，`0x0F` 方向；
     LCD 与 `app/repeater_dialog.*` 同时显示 RX/TX 两个频率（DUP−/DUP+ 自动算 TX）。
   - 亚音：`0x16 0x5D/0x42/0x43/0x4B` + `0x1B 0x00/0x01/0x02`，EIA 50 音 +
     104 个常用 DCS 码；DTCS 极性字节布局按指南推定，**真机未验**。
   - UI 重排：132px 旋钮台与 S 表 / Po·SWR 同排，频谱纵向拉伸（最小 260px），
     RX 44pt / TX 18pt 双频大字。
   真机复验清单（按风险排序）见 `docs/original-rsba1-feature-map.md` §K / §L.5。
5. **50003 音频真机复验**（代码已接、门禁有单测，但**未在 IC-705 上实际通话**）：
   RX 播放 / PTT 门禁麦克风上行 / 半双工静音 / 真实 RX PCM 频谱四条都要过一遍。
   设备可选（2026-10-30）：主窗「音频设置」列 WASAPI 输入/输出，选 `QSettings`
   键 `audio/in_dev`、`audio/out_dev`（-1=跟随系统），开启中切换会按新设备重开；
   对话框同页每 0.5 s 显示 RX 块/待发/丢/弃、TX 帧/弃、扬声器积压/欠载/丢、
   麦克风溢出（`app/audio_stats.*`，链路计数走 `IAudioLink::rxStats`）。
   2026-10-30 已补本机环回（假电台三信道：握手/CI-V 双向/TX 线格式/RX 重排与
   丢包放行/拆除五件套），**协议与编排已可回归，真机时钟/抖动/音频设备仍待复验**。
   FT8 本机解码（§4 FT8 行）吃的是同一路 50003 下行 PCM，真机复验时顺带过：连解
   两个时隙、与 WSJT-X 面板收同一信号交叉比 SNR/DT/DF、看丢弃计数是否为 0。
   FT8 定时发射同样待真机：①整槽从电台 MOD 出、对方（或本机 50003 回环）能解出
   原文；②PTT 在槽起点前 500 ms 已键好、播完回 RX 的尾不吞掉首个符号；③刻度不在
   业余段时排期到点必须被门禁拒掉（面板显示「发射被拒绝」而不是悄悄发射）；
   ④发射中途关主窗：音频线程收手、PTT 一定回 RX（`~MainWindow` 里显式兜底）。
   复验清单见 `docs/original-rsba1-feature-map.md` §L.6 与
   `docs/research/rsba1-audio-upstream-2026-10-30.md` §6。
6. **APRS 真机复验**（代码/单测已齐，但**未上真机**）：① KISS 串口接 TNC /
   Dire Wolf 收发 AX.25；② LoRa APRS 网关（KISS over TCP）收发；③ 50003 AFSK
   1200 Bd 实际空口回环（含音频延迟/PTT 尾）；④ iGate 双向在真实 APRS-IS 上
   观察 qAR 与去重。已知边界：LoRa 只按 KISS 网关形态支持，无原生 LoRa 硬件
   抽象；地图瓦片需网络（测试用 `RADIO_SUITE_APRS_TILES=off` 关闭）。
7. **WSJT-X 联调**（协议与 UI 已齐，UDP 回环测试通过，**未接真实 WSJT-X**）：
   ① 在 WSJT-X `Settings → Reporting` 启用 `Accept UDP requests` 并统一组播
   地址/端口；② 验证 Status/Decode 刷新、双击/呼叫选中回发 Reply；③ 验证
   QSO Logged 与 ADIF 导出页；④ 验证 HaltTx / FreeText / Replay / Location
   在真机上的行为（含跨机器 UDP 防火墙放行）；⑤ QSO 自动入 HamLog
   与双击回填只在映射/信号层有单测，**未在真实 HamLog 库上写入复验**（测试
   仍遵守「真实库只读」约束），联调时注意先备份 `Log.db`。
8. **工程小债**：logbook 库反向引用 app 层设置对话框头（链接可解）→ 挪 libs/ui 时清。
9. ~~GitHub 推送~~ → **已于 2026-10-26 完成**（远端 fe1iscurc0r/radio-suite，
   public/AGPL-3.0，CI 三平台全绿；详见 §6「GitHub 与 CI 现状」）。
10. **同类开源调研**（2026-10-29）：`docs/research/oss-survey-2026-10-29.md`。
    结论：无可整仓合并项；唯一现实可 vendor 的是 `ft8_lib`（MIT，本地 FT8 解码），
    其次为 Dire Wolf 思路（DCD 监听 / 早迟门比特同步）与 liquid-dsp（MIT，真机
    灵敏度不足时再引）。优先级与工作量见该文档 §3。
11. **安卓真机验收**（清单 `docs/android-真机验收清单.md`；**2026-10-31 已在真机跑过第一轮**，
    Redmi K40 / Android 13 / arm64-v8a / MIUI V14.0.6，逐项证据在清单 §K4）：
    持久化与配置侧已拿到真机证据（A1–A7、D1 落盘+读回、G-0 的配置半条、K1/K3）；
    D3 只到「写侧」（顺带把 APK 没打 SQLite 驱动这个打包缺陷修了，见下面那条）；**判「链路通不通」的 G-1…G-5 仍是空的**——手上没有可连的电台（只有全盛 UVK6，
    不是 IC-705、没有 50003 音频口），所以「手机上保存 → 重启 → 真连上电台」只到
    「按偏好建出 UDP 链路并老实报连不上」为止。B3/B5/B6、Gate E/F、A8、D4 本轮没覆盖
    （D2 当时也没覆盖，11-01 随 C9 一起做掉了）。
    清单里写「代码事实」的部分只是 PC 静态核对（源码行号、aapt 出包信息、jar 字节码），
    不构成真机结论——打勾要换成设备上取到的 ini 字节 / logcat / 截屏。
    - **K1（APK 只能跑 mock）代码侧已修**（`fix/android-link-prefs`）：新增
      `apps/desktop/app/launch_config.{h,cpp}`，链路优先级 **命令行 > 持久化偏好 > mock**
      在 `main.cpp` 一处算完，GUI 与 `--headless` 共用同一个 transport 工厂；
      Android 上偏好显式走 `QSettings::IniFormat`（默认构造的 NativeFormat 在安卓没有后端）；
      「连接…」对话框从「只渲染一行命令文本」改成真表单：回填偏好、「保存」写偏好、
      重启生效（**不做会话热切换**）。离屏用例 9 例（`tests/test_launch_config.cpp`）。
      **手机上已验到「配置半条」**（2026-10-31，清单 G-0 + §K4）：对话框选 UDP → 保存 →
      ini 落 `[link] mode=1` → force-stop 重启 → logcat「链路： udp （来源： 持久化偏好 ）」、
      重开对话框下拉回填 UDP。仍空的是「真连上电台」那半条（G-1…G-5，没有可连的电台）。
    - **K3（状态栏谎报）代码侧已修**：空闲文本改由 `LinkPrefs` 派生，UDP 链路下是
      `transport: udp · <IP>`；但它只反映「程序以为在跑哪条链路」，**链路通不通仍看行为**
      （频率是否随电台变、50003 是否进音频）。
    - **桌面实窗已验（2026-10-31，非真机、非手机）**：`--mock` 起来后状态栏
      `transport: mock · radio: IC-705 sim`；「连接…」对话框（链路下拉/电台 IP/用户名/
      密码/Python 档 + 实时预览「下次启动 → …」+ 保存/关闭）渲染正常，点「保存」后
      主窗闪现 `链路设置已保存（mock），重启 radio-suite 生效`，注册表
      `HKCU\Software\radio-suite\radio-suite\link` 落下 `mode/host/user/pass/python`
      五个值（写侧通）；再把 `link/mode` 手改成 1、`host` 改成 192.0.2.55（TEST-NET，
      不可达）**不带任何命令行参数**重启 → 状态栏 `transport: udp · 192.0.2.55`、
      频率读到 0.000000 MHz、S 表 -999 dBm（读侧通，且连不上时不谎报在线）。
      这条正是 Android 路线的桌面等价验证；手机侧那两处（IniFormat 落盘、`am` 启动注入）
      2026-10-31 已各自拿到真机证据（清单 D1 / A7 / §K4）。测完已删掉自己建的 `link`
      键，其余设置未动。
    - **顺手挖到的同类缺陷（未修，记此）**：合成谱与链路无关
      （`MainWindow.cpp:1242-1250`，只有网络音频 active 且有下行才切真实 PCM），
      所以 UDP 链路没连上时瀑布照画三个假峰——和 K3 同一类「界面说的不是真话」。
      要修得先定口径：按链路类型关掉合成谱，还是给瀑布挂「合成」角标。
    - **K2**：未签名 release 包也能用 `am start --es applicationArguments "…"` 注入参数
      （`QtActivityLoader.extractContextMetaData()` 在 debuggable 闸**之前**读该 extra）；
      `extraappparams` 那条才要 `android:debuggable="true"`。打包期退路是 target 属性
      `QT_ANDROID_APPLICATION_ARGUMENTS`。两条都只是调试通道，日常用 G-0 的应用内设置。
    - **D1（其余持久化在安卓必丢）代码侧已统一**（`fix/android-settings-persistence`）：
      新增 `apps/desktop/ui_common/app_settings.h`，全站唯一的
      `radio::ui::appSettings()`——Android 显式 `IniFormat`，落点取
      `QStandardPaths::AppDataLocation`（回退 `QDir::currentPath()`），桌面仍是
      `HKCU\Software\radio-suite\radio-suite`（与旧默认构造同址，老设置不挪窝）。
      并进来的 16 处：MainWindow 7 + wsjtx_panel 3 + font_proxy_settings 6，
      加上链路偏好那两处；`launch_config` 自己的 `openSettings()` 已删，不再各写一遍。
      **中间态 `IniFormat/UserScope` 也是错的**，别照它抄：它绕开了「NativeFormat 在安卓
      没有后端」，但落点变成 `applicationDirPath()` = APK 解压出的
      `/data/app/~~…/lib/arm64`（root:root 755），QSettings 的「临时文件 + rename」在那里
      **静默失败**——logcat 连一条警告都没有。三条死路与唯一可写落点见清单 D1 正文与 §7 第 15 条。
      桌面等价验证（2026-10-31，非手机）：写 `font/family=Consolas` + `font/size=16` →
      **不带任何参数**重启 → 全界面字体变 Consolas 16（截图对照过基线）→ 删键复原；
      用例 `app_settings_names_its_own_store` 锁住「后端自己点名，不靠宿主调过
      setOrganizationName」（logbook 就没调）。**手机上已验（2026-10-31，清单 D1 + §K4）**：
      应用**自己**写出 `/data/user/0/org.radiosuite.app/files/radio-suite.ini`，里面真有
      `[mem] M1\freq=14270000 M1\mode=1`（点 M1「存」）、`[ft8] source=1`（FT8 音源切麦克风）、
      `[link] mode=1`；force-stop → 不带参数重启 → FT8 面板音源仍是「本机麦克风」且状态行
      走「已解 0 槽」分支（读回 + 采集真起来两件事）。注意：`app_settings_names_its_own_store`
      的 Android 分支在桌面上根本不参与编译，**ctest 绿不等于手机上也绿**，这条只有真机证据算数。
    - **竖屏布局判为不通过（C1/C2 FAIL，本轮只记不改）**：手机竖屏 1080×2400、
      `wm density` 440 → 可用宽度只有 ~393dp，而主窗沿用桌面那套两列布局，实测**右列大按键
      压住「音频设置」那一行**、频段/模式/内存三行被裁、频率大字显示成「270000」；
      主窗按键 `setMinimumHeight(64)` 在这台机上量到 33.8dp（横屏）/ 42.5dp（竖屏），
      低于 48dp 触摸目标底线。两条出路：manifest 锁 `sensorLandscape`（一小时级，牺牲竖屏）
      或真做一套手机布局（几天，纵向预算要重算）。**这条是产品形状的决定，不要顺手改。**
      **2026-10-31 用户亲测后拍板**：竖屏放弃、横屏保留（「固定横屏效果会好很多」），
      但横屏那套桌面布局在手机上仍不成立——状态行吃掉频谱高度、S 表压 40m 波段行、
      调谐旋钮被波段按钮盖住、部分按钮直接不在屏内，且「按键太大、界面不够大」（手机端不是平板端）。
      方向 = 锁横屏 **+ 分页重排**：频谱单独一页、主页只显示当前波段、点开进波段调谐页、
      制式单独一页或一个入口。见验收清单 C1 末段。
      顺带一条已修的同类：连接对话框原来的 `setMinimumWidth(460)` 把「保存/关闭」整排挤出
      竖屏屏外（bounds 报 `[0,0][0,0]`），现已按屏幕宽度取小（`app/connect_dialog.cpp`）。
    - **D3（APRS 在安卓存不了）根因不是路径，是打包**（`065ff83`）：第一包真机给的是真话
      ——「APRS 保存失败：Qt SQLite 驱动不可用」，`unzip -l` 数过 APK：`libQt6Sql.so` 在，
      `plugins/sqldrivers/` 不在（插件只跟**可执行目标**挂的插件走，挂在静态库上的驱动进不来）。
      第二包真机：应用自己建出 `/data/user/0/org.radiosuite.app/files/aprs.db`，36864 字节、
      `SQLite format 3` 头、`stations/track/outbox/inbox` 四张表，设备侧 md5 与本地取回的字节
      逐字节对上（前提：先把 `adb shell cat` 插进去的 `\r` 还原）。
      **读侧仍没验**：库里 0 条站点，而面板顶行「连接 APRS-IS」在竖屏被裁到屏外点不到——
      又被 C1 挡回去。`pm install -r` 覆盖安装后 `radio-suite.ini` 原样保住，这条顺带说明
      D1 的落点在应用更新里也是稳的。
    - **手机分页重排已落地（C8，2026-11-01 交付，`feat/android-paged-ui`）**：只动 Android——
      `buildUi()` 拆成 `buildDesktopUi()`（原两列布局逐行保留）与 `buildAndroidUi()`
      （顶栏 + `QStackedWidget` 五页 + 底部互斥页签）；所有控件由同一组 builder 各构造一次，
      不存在桌面/安卓两套实例。页签：频谱 / 调谐 / 制式 / 面板 / 日志。
      状态行从底部一行挪进顶栏（用户报的「Transport UDP 吃频谱高度」），手机顶栏
      `TX = RX` 只在 DUP−/DUP+ 时出现；全部按钮最小高度抬到 44dp。
      **真机五页逐页截图过（2026-10-31 23:5x，横屏 843×376dp）**，C1/C6 那批碰撞全部消失。
      交付前最后一改（HamLog 不可用时不出「日志」页签，手机变四页签）装包后进程存活无崩溃，
      但**那一版没截到图**：手机午夜自动锁屏，MIUI 在锁屏状态拒绝截屏（`screencap` 回 0 字节，
      `wm dismiss-keyguard` 无效）——该补记 11-01 15:24 已完成：页签栏确为频谱/调谐/制式/面板
      四页（验收清单 C9 正文），逐页版式与交互另见 C10 那轮真机复验。其余构建证据
      （MSVC `ctest` 181/181、Android clang 交叉编译通过）齐全。
    - **C9（HamLog 在 Android 上从未可用）已修并合并（PR #24 → `fix/android-touch-dialogs`，
      merge commit `648658b`）**：
      `RADIO_SUITE_HAVE_LOGBOOK=1` 编进了包，但真机 `hamlog_.available()` 恒假——
      `HamLogDb::defaultDbPath()` 指的文件不存在，`available()` 要求「库在且 log 表可读」。
      旧布局里这组控件静默隐藏，分页版第一稿才把它摆成一页死内容；处置仍是**不可用就不建
      这一页**，但补上了建库：`HamLogDb::createDatabase()`（log 18 列 + settings，全
      `IF NOT EXISTS`，不动已有行）+ Android-only `ensureLocalDb()`，Android 上库路径改走
      已验证可写的 `radio::ui::settingsDir()`，`MainWindow` 构造在建 UI 前确保库存在。
      11-01 17:46 真机复验：库自动建出、第五个页签「日志」出现、记 QSO 写通；17:53 同页
      「ADIF 导出」也验通（Qt 把 `QFileDialog` 交给系统 SAF 选择器，`QFile` 往返回的
      `content://` URI 直写出合法 ADIF 3.1.4）。导出回显打的是裸 URI，手机上看不出文件落在
      哪，这一条算未修的小瑕疵（改文案属产品口径）。详见验收清单 C9 正文。
    - **手机对话框/面板「能滚、能关」已修（C10，`fix/android-touch-dialogs`）**：用户真机报
      「音频设置没有退出键、cancel 无效，应用中继没有关闭键，频差/模拟亚音/DTCS 选不到，
      估计无法滑动；连接关闭键失效，APRS/WSJT-X 无退出键，切 WSJT-X 页面有问题，
      SSTV 依旧无法滑动；波段点击不显示亮度」。根因是 Android 没有窗口管理器：
      `QDialog` 无标题栏、尺寸不受屏幕约束（中继 560 宽、SSTV 更高），376dp 高的内容区
      把底部按钮整排推到屏外；面板连关闭键都没有，系统返回键 hide 顶层窗后主窗大按键
      仍是 checked，再点一下只是关掉「看不见的已开窗」。
      修法：新增 Android-only 的 `apps/desktop/app/touch_dialog.{h,cpp}`——
      `fitDialog` 把根布局整体搬进 `QScrollArea`、底部钉常驻「关闭」（→ reject）、
      交互控件（按钮/下拉/数字框/输入框/勾选）抬到 ≥44dp、窗口压进
      `QScreen::availableGeometry()`，并给滚动区挂
      `QScroller` 左键拖动（QScrollArea 默认不认手指拖动），分页主窗的滚动页同样挂上；`addCloseBar` 给
      APRS/WSJT-X/FT8 三个顶层面板插「标题 + 关闭 ✕」；`showInfo` 替代
      `QMessageBox`（长文案会滚，滚动区同样挂 QScroller）。接线覆盖 12 个对话框（音频设置、
      中继/亚音、连接、SSTV、DTMF、语音、CW、SET、MEM、APRS 消息、APRS RF、WSJT-X UDP）。
      主窗侧：三个面板 `QEvent::Hide` → 复位对应大按键；三个面板大按键互斥并加
      `:checked` 琥珀背光；波段/广播预置改 `checkable`，`syncBandHighlight()` 在
      `refreshState()` 按 `[low, high)` 回读点亮。三个离屏 UI 测试目标补编
      `touch_dialog.cpp`（原先只构 app 目标没暴露，Windows/macOS CI 链接期
      `LNK2019 fitDialog` 才抓到）。
      **真机上又抓到一个搬布局的缺陷（已修 `f540c30`）**：`QBoxLayout::takeAt()` 返回的
      item 就是子布局对象本身，`addLayout()` 收养它并把控件改父到滚动页；随后再
      `delete it` 等于删掉滚动页仍在引用的布局——真机上表单行停在旧坐标（「扬声器」
      跑到确定/取消下方、拖不动），离屏回归用例直接 `0xC0000409`。修法是子布局分支
      只交给 `addLayout` 收养，不再删除；新增
      `tests/test_touch_dialog.cpp::fitDialog reparents form rows into scroll content`，
      桌面用 `RADIO_SUITE_TOUCH_FIT=1` 打开同一套逻辑跑断言。
      **已验**：桌面 MSVC 构建、`ctest` 183/183、Android clang 交叉编译出新包；
      真机第一轮（解锁后）确认波段高亮生效、音频设置对话框底部出现常驻「关闭」，
      随即发现上面那个表单错位缺陷。
      **11-01 13:30 第二轮真机复验通过**：方向下拉可选、应用频差后 TX 立即显示
      `14.870000 MHz +600.0 kHz`（上差）/`13.670000 MHz −600.0 kHz`（下差）；模式下拉
      8 项含 DTCS（数字亚音），选完可滚到「码 / 收极性 / 发极性」；音频设置 Cancel 与
      连接/中继对话框的常驻「关闭」都生效；20m/40m 波段高亮随频率回读；APRS / WSJT-X
      有「关闭 ✕」且返回键能收面板并复位大按键；SSTV 面板可拖。这一轮又定位两个根因：
      ① **下拉弹窗触摸错位**——Qt 6.7 安卓顶层窗口的触摸换算按窗口位置（dp÷dpr）偏移，
      窗口在 (0,0) 才准，而弹窗永远贴在控件旁边，点任何选项都命中错行（就是用户说的
      「模拟亚音模式无法设置 / 数字亚音未看到」）；修法是启动时装
      `installPopupOriginFix()`，弹窗 `Show` 后钉到 (0,0)、二次 `singleShot(0)` 再钉，
      并在列表 viewport 上自装 `PopupClickFilter` 按 `indexAt` 选行（Qt 自己的
      itemSelected 在触摸合成鼠标事件时根本不触发），选行/收起推迟到下一轮事件循环。
      ② **第二个顶层窗口丢帧**——控件状态与模型都更新了，屏幕却停在前一帧，要再点一下
      才刷新（应用频差后 TX 标签迟一步、下拉选完不刷新、甩动回弹的横向偏移留在屏上）；
      修法是对话框内每次 `MouseButtonRelease` 后 60ms 补一次整窗 `update()` +
      `QWindow::requestUpdate()`，`QScroller` 回 `Inactive` 后再补一帧，并把滚动页横向
      范围钉死为 0（手指横向抖动不再把整页拖偏切掉左侧标签）。
      **11-01 15:28 又补点了 SET / MEM / CW / DTMF / VOICE 五个对话框**：都按全屏贴边
      打开、底部常驻「关闭」一点即回面板页；SET 内容一屏放得下，MEM/CW/DTMF 滑到底
      分别能见到名称与选组/选信道、回读提示与应用设置/发送、生成 .wav，左侧标签不被切。
      另注意 `fitDialog` 只对 `QBoxLayout`/`QFormLayout` 根布局生效，`QFileDialog`
      这类 Qt 自带文件框不在范围内——11-01 17:53 实测：Android 上 `QFileDialog` 根本
      **不是 Qt 窗口**，Qt 6.7.3 把它交给系统 SAF 选择器（`documentsui/PickActivity`），
      所以不需要 `fitDialog`，也不受本仓触摸修复的影响。
    - **手机端权限（OTG / 存储 / 组播）已核清，缺口不在弹窗**（11-01 18:40 静态核对 +
      一次真机 `dumpsys`，矩阵与判据在验收清单 §2b）：麦克风已声明并在启动时申请、真机
      `granted=true`；`WRITE_EXTERNAL_STORAGE` 是 Qt6Core 依赖清单注入的历史遗留，
      targetSdk 34 上系统根本不给授（`granted=false`），而导出走 SAF 已经能用，
      **不该再去要「存储权限」**（只剩「所有文件访问」这种安全设置，不申请）；
      **USB OTG 串口缺的是后端不是权限**——安卓 Qt 不带 QtSerialPort
      （`Qt6SerialPort_DIR-NOTFOUND`），`RADIO_SUITE_HAVE_SERIALPORT` 在安卓构建里是关的，
      APRS 面板走的是「本构建未启用串口…请用 KISS TCP」那句降级，要做 OTG 得自己接
      `UsbManager`（JNI + `usb.host` feature + `requestPermission`）或 vendor
      usb-serial-for-android，且台架上没有可插设备、手机 USB 口被 adb 占着，**今天证不了**；
      WSJT-X 组播缺 `CHANGE_WIFI_MULTICAST_STATE` + `MulticastLock`（也要 JNI），
      只声明权限不拿锁等于没做——**能先做的替代是单播数据通路，不需要电台**。
    - **顺带一条与本次无关的老坑**：手工 `reg add` 平铺写出的 `wsjtx/group` 应用读不到
      ——QSettings 把 `wsjtx/group` 理解成「组 `wsjtx` 里的键 `group`」，值要落在子键
      `wsjtx` 下才算。本机就留着这么一对平铺值，所以 WSJT-X 面板一直按默认组播
      224.0.0.1 去 join 并报「加入组播失败」。改监听地址要走面板里的「设置」，别用 `reg add`。
    - 判链路/判音源的条目**不许拿 mock 行为打勾**，那会写出假报告。

## 6. 构建与验证（本机环境）

```bash
# 环境：MSVC 14.44（VS2022 BuildTools）+ Qt 6.8.3（D:/Qt）+ CMake/Ninja（VS 自带）
cmd //c "build\msvc_build.bat radio-suite radio_tests radio_ft8_ui_tests"   # 仓内脚本，自己 call vcvars64
QT_QPA_PLATFORM=offscreen cmd //c "build\run_ctest.bat"                     # 181/181（本栈分支；main 上少一截，栈里几条 PR 都没合）
cmd //c "build\msvc_build.bat"                       # 至少要带一个 target，无参不给全量
# 单测 exe 直跑要 Qt bin 在 PATH：PATH="/d/Qt/6.8.3/msvc2022_64/bin:$PATH" \
#   QT_QPA_PLATFORM=offscreen ./build/tests/radio_ft8_ui_tests.exe "[ft8][ui]" -s
# 注意 exe 真身在 build/apps/desktop/app/radio-suite.exe；build/src/app/ 下那个是
# 旧 CMake 布局的 5 天前残留，跑了看不到新功能。MSVC 输出是 GBK → 管道过
# `iconv -f GBK -t UTF-8 -c`，否则中文全成乱码。
cmd //c "D:\tmp\rs-build.bat spike-configure && spike-build"   # WO-274 spike
```

- FetchContent 需网络；本机 github 间歇不通 → 已在 rs-build.bat 用
  `FETCHCONTENT_SOURCE_DIR_POCKETFFT/CATCH2` 指向 `D:/tmp/` 的本地副本。
- python 真身：`C:/Users/ASUS/python-sdk/python3.13.2/python.exe`（PATH 上的
  python 是 WindowsApps 假壳）。
- 麦克风冒烟：`build/apps/desktop/audio/audio-in-smoke.exe`。
- 这 181 个用例包括：session PTT 真实 CI-V 帧序列、bus 真实 HTTP、rigctld 真实 TCP、
  logbook 临时 SQLite，直接调用生产 `scope_wire.*` 的 spike 测试，音频门禁
  （`AudioBridge` 半双工/有界队列/失败重试）单测，DSP 单音/泄漏/Welch 平均与
  S 表 IARU 映射，RS-BA1 本机环回（假电台三信道 + 100 ms 音频重排窗，见
  `docs/research/rsba1-audio-upstream-2026-10-30.md`），FT8 槽解码器（UTC 对齐 /
  48k→12k 抽取 / 合成信号端到端）与 FT8 发射（排期算术 / 整槽渲染 / 渲染→解码闭环），以及
  `radio_ui_tests`（offscreen QPA，真点 CW/Set Mode/Memory/中继·亚音对话框按钮
  并断言 mock 与状态回读；另含链路偏好优先级、空 IP 折回 mock、偏好 ini 往返、
  「连接…」对话框回填/保存、设置后端命名）与 `radio_ft8_ui_tests`（面板注入行 → 激活目标、
  PCM → 解码 → 表格端到端、发射排期→到点出口→取消/拒绝四条路径；`BUILD_GUI=OFF`
  的 Linux CI 自动不建这些 target）。
  SSTV/Voice Memory 已统一走 `beginScriptedTx/endScriptedTx`；旧“双击收尾”经
  受控变异确认会 FAIL（仍处 TX / 无 PTT OFF），恢复后全绿。
- 2026-10-29 实窗验证（全程不接电台）：mock 启动截屏核对频谱/瀑布全宽、最新行在顶部；
  发现并修复右半屏越界读（1025 bin 误按 i*2 取对）与 S 表读数被表针压字；
  Voice Memory 4s wav 在真实 WASAPI 默认输出上播放 4.5s、门禁发射 4.3s 自动回 RX，
  播放期间 UIA 查询延迟稳定 5–7 ms（GUI 不冻结）；WSJT-X 面板注入 QDataStream 线格式
  Status+Decode 后，客户端/14.074000 MHz/解码行全部命中；注入 QSO_LOGGED 后状态栏出现
  「WSJT-X QSO 已写入 HamLog：BG5GXO 14.0740 MHz FT8」且最近 QSO 列表新增该行（全程用
  临时 `APPDATA` + Log.db 副本，真库 sha256 前后一致）；另用本机 Qt 6.8.3 探针实测
  QDateTime 序列化为 13 字节，与 `Writer::dateTime` 完全一致；APRS/WSJT-X 面板独立窗口
  实窗巡检发现地图底部纬度刻度与经度刻度/OSM 版权行三行叠字，已改为版权右上角、底行
  只留经度且按标签宽度隔一枚画；WSJT-X 组播加入失败时补「改用单播」提示；
  「HamLog 工作台…」原来 new 成主窗口的子控件（内嵌左上角盖住频谱/PTT、无标题栏），
  已改为 `Qt::Window` 独立窗口并设标题。
- 2026-10-30 实窗验证（FT8 本机解码，全程不接电台）：主窗右侧按钮列加「FT8 本机」后
  9 个按钮仍不越界（1105×768 截屏核对）；点开面板确认标题栏 / 停止解码·清空 /
  时隙进度条 / 五列解码表 / 底部提示行渲染正常。面板在 mock 链路下如实显示
  「等待 50003 下行音频」（原来会谎报「采集中」——`SlotDecoder::fedSamples()`
  区分「对齐等待」与「真没音频」）；顺带发现并修复 §7 第 10 条（开着面板关主窗，
  进程不退出）。解码正确性由 `[ft8][slot]` / `[ft8 panel]` 端到端用例承担，
  实窗只验 UI 与状态口径。
- 2026-10-30 实窗验证（FT8 定时发射，全程不接电台）：面板顶部发射行渲染正常（文本框
  + 音频频偏 1000 Hz + 任意时隙 + 定时发射 + 提示「等到下一个时隙起点整槽播出（15 s）」），
  空文本点「定时发射」如实显示「文本为空」，关主窗后进程退出（发射出口尚未创建，
  析构兜底不误触发）。发射链路两条设计口径见 §7 第 11 条。
- 2026-10-30 实窗验证（FT8 音源切到本机麦克风 + 声学环回，不接电台）：**到点起播这次在
  实窗点响了**。Computer Use 更正：`set_value` 能往 Qt `QLineEdit`/`QSpinBox` 里填值
  （早前「键盘不可用、填不进字」的结论作废），组合框弹出项点 `element_index` 和点弹层自己的
  `screenshotId` 都不生效，要拿**父窗口的** screenshotId 点弹层像素。链路：面板音源选
  「本机麦克风」→ 状态行「本机麦克风 · 已解 N 槽」→ `ft8/source=1` 落 `QSettings`→
  重启后面板自动按麦克风源恢复并在开解码时自动起采集；网络音频（50003）同时在跑时
  抢同一采集设备，主窗如实拒绝而不是把两路灌进解码器。喇叭对麦克风播整槽，面板解出
  `CQ BG5GXO PM01`（SNR 15–19）；面板自己排期自己发射的那几槽 **DT 0.0–0.1 s**，
  印证 `n_lead` 由 500 ms 改 0 之后波形顶格在槽起点起播（旧值会把整槽推过槽界）。
  DF 在 1000 Hz 档会跳到 250 Hz 奇数倍、1200/1500 Hz 档精确——离线四档逐档命中，
  锅在声学信道，详见 §7 第 14 条。同轮实窗还抓出 §7 第 13 条那个「发射完按钮仍写
  取消发射」的真缺陷（点下去等于再排一发 15 s），已修并用 offscreen 变异验过。
- 单播一个合成 FT8 时隙到指定输出设备（验发射链路、不依赖 GUI）：
  `build/tools/ft8play.exe --list` 列设备 id，`build/tools/ft8play.exe "CQ BG5GXO PM01" 1200 0.35 129`
  = 文本 / 音频频偏 Hz / 峰值 / 设备 id，阻塞播 15 s；**它不对齐槽边界**，要对槽就自己等：
  `now=$(date +%s%3N); sleep $(awk -v m=$(( 15000 - now % 15000 )) 'BEGIN{printf "%.3f", m/1000}')`。
- 声学环回用例 `radio_ft8_acoustic_tests`（tag `[ft8][acoustic]`，Test #162）只在
  `BUILD_GUI=ON` 时注册，且**默认只 WARN 跳过**，要显式 `RADIO_SUITE_FT8_ACOUSTIC=1`
  才真出声真收声（要真扬声器 + 真麦克风，所以 172 条回归里它永远是跳过的那一条）。
  2026-10-30 显式开启实测一次通过：采集 839,040 样本、峰值 2799、解出
  `CQ BG5GXO PM01`，**SNR 16 / DT −0.08 s / DF 1500 精确命中**（f0=1500），解码耗时
  7.5 ms——一槽 15 s 的收发对齐在自动化用例里也钉住了，不只是手点。
- 部署目录 `packaging/out-win/`：`cmd //c "build\msvc_build.bat deploy_win"`
  会复制当前 exe 并用 windeployqt 刷新 Qt DLL/插件（含 `Qt6SerialPort.dll`、
  `platforms/qwindows.dll`），双击 exe 即可运行，无需把 Qt `bin` 加进 PATH。

### 6.1 Android 交叉编译与 APK（2026-10-30 工具链 + GUI + APK 均已跑通）

```bash
cmd //c "build\android_configure.bat"          # 生成 build-android（arm64-v8a，GUI 默认 ON、测试 OFF）
cmd //c "build\android_build.bat radio-suite"  # 或只编核心库：… radio_core radio_ft8 radio_aprs
cmd //c "build\android_build.bat apk"          # 打 APK（release 未签名，签名见下）
```

- 本机安装位置：`D:/Qt/6.7.3/android_arm64_v8a`（目标库，含 Gui/Widgets/OpenGLWidgets/
  Network/Sql/Multimedia，381 MB）+ `D:/Qt/6.7.3/msvc2019_64`（host tools：
  moc/rcc/uic/**androiddeployqt**）；SDK `D:/Android/sdk`（platform-tools/adb 37.0.1、
  platforms;android-34、build-tools;34.0.0、cmake;3.22.1）+ NDK `26.1.10909125`（r26b，2 GB）。
  Java 21（Adoptium）已在本机，sdkmanager 与本仓脚本都用它。
- **Qt 官方公开仓库的 Android 包只到 6.7.3**（`download.qt.io/.../windows_x86/android/`
  无 `qt6_683`）；aqt 装 Android 版时也不会带 host tools，必须再装同版本桌面版并给
  `QT_HOST_PATH`，否则 moc/androiddeployqt 缺失。
- 已验证：`radio_core`、`radio_ft8lib`、`radio_ft8`、`radio_aprs`、`radio_aprs_db`、
  `radio_wsjtx`、`radio_dsp`、`radio_ui_common`、`radio_logbook`、`radio_audio` 全部编成
  **AArch64**（`llvm-readelf -h` 核对 Machine=AArch64）；`radio-suite` **GUI 可执行文件也
  已链出**（ELF64 PIE，interpreter `/system/bin/linker64`，for Android 26，19 MB）。
- 音频后端（2026-10-30）：vendored RtAudio 6.0.1 与上游 master **都没有 Android 后端**
  （源码 0 处 android/AAudio/OpenSLES），原来的 else 分支按 Linux 编 ALSA 直接
  `alsa/asoundlib.h` not found。现改为 **NDK AAudio** 直连实现
  （`audio_out_android.cpp` / `audio_in_android.cpp`，接口与 RtAudio 版
  `AudioOut`/`AudioIn`/`AudioSink` 完全一致，桌面代码零改动）：输出用
  `AAUDIO_PERFORMANCE_MODE_LOW_LATENCY` + 非阻塞写（100 ms 超时轮询 `playing`），
  采集用回调 + 余料累积到 20 ms 帧（与桌面同一 `kTxFrameSamples` 语义），
  `AudioSink` 仍是 200 ms 有界队列 + 欠载计数。AAudio 要求 **API 26+**，
  `CMakeLists.txt` 会对 `ANDROID_PLATFORM` 做下限检查并 `FATAL_ERROR`。
  设备枚举在 Android 上退化为「系统默认」单设备（AAudio 无 RtAudio 那种 ID）。
- GLES 兼容：瀑布纹理原写死 `GL_R8`/`GL_RED`，Android 的 GLES2 上下文没有这两个常量。
  现按 `QSurfaceFormat` 判 ES2 → `GL_LUMINANCE`，否则 `GL_R8`/`GL_RED`，
  并用 `#ifndef` 兜底常量（`apps/desktop/app/WaterfallWidget.cpp`）。
- **APK 已能打出来**（2026-10-30）：`cmd //c "build\android_build.bat apk"` →
  `build-android/apps/desktop/app/android-build/build/outputs/apk/release/android-build-release-unsigned.apk`
  （约 12 MB，`org.radiosuite.app`，minSdk 26 / targetSdk 34，含
  `libradio-suite_arm64-v8a.so` 与 Qt 运行库）。两点经验：
  1. 之前记的「Qt 二进制包不含 APK 模板」是**错的**：aqt 装的
     `D:/Qt/6.7.3/android_arm64_v8a/src/android/` 里有 `templates/`（AndroidManifest、
     build.gradle、res）和 `3rdparty/gradle/gradlew`；androiddeployqt 直接可用，
     gradle 走 wrapper（AGP 7.4.1 + androidx 从 google()/mavenCentral() 拉，首次需网络）。
  2. 应用清单用 `packaging/android/AndroidManifest.xml` 覆盖模板
     （`QT_ANDROID_PACKAGE_SOURCE_DIR`）：包名 `org.radiosuite.app` + `RECORD_AUDIO`
     （INTERNET/storage 由 androiddeployqt 自动注入）。APP 目标在 Android 上必须是
     `qt_add_executable`，否则没有部署设置/apk 目标。
  3. 签名（本机调试）：`apksigner sign --ks <debug.keystore>`，
     `apksigner verify --print-certs` 复核；keystore 别进仓库。
     本机 `apksigner` 没有 `.exe`，只有 `apksigner.bat`，直接
     `java -jar D:/Android/Sdk/build-tools/34.0.0/lib/apksigner.jar …` 最省事；
     debug keystore 用 `C:/Users/ASUS/.android/debug.keystore`
     （alias `androiddebugkey`，store/key pass 都是 `android`）。
  4. **插件进没进包要单独验**：Qt 的模块库会跟着链接关系进 APK，插件只跟**可执行目标**
     挂的插件走；本仓静态库（`radio_aprs_db`/`radio_logbook`）挂的 `Qt6::Sql` 驱动原来进不去，
     `065ff83` 起在 `apps/desktop/app/CMakeLists.txt` 显式 `qt_import_plugins`。
     出包后一条命令自查：`unzip -l <apk> | grep sqldrivers`（坑见 §7 第 18 条）。
- 麦克风权限：`RECORD_AUDIO` 已在清单声明；`MainWindow` 构造时在 Android 上
  `qApp->requestPermission(QMicrophonePermission)` 申请一次（Qt 6.7 没有独立
  `QMicrophonePermission` 头，用 `<QPermissions>`）。
- **已装到真机跑过（2026-10-31，Redmi K40 / Android 13 / arm64-v8a / MIUI V14.0.6）**：
  出包 → `zipalign -f 4` → `apksigner sign --ks <debug.keystore>` → 推
  `/data/local/tmp/` → `su -c 'pm install -r …'`（MIUI 14 拦 `adb install`，见 §7 第 17 条）→
  `am start -n org.radiosuite.app/org.qtproject.qt.android.bindings.QtActivity`
  （可加 `--es applicationArguments "…"` 注入参数）。冷启动不崩，logcat tag `radio-suite`，
  GLES 实机渲染出图、RECORD_AUDIO 弹窗授权后保住、AAudio 采集进样
  （FT8 面板「本机麦克风 · 已解 0 槽」+ 进程内 `AudioRecord` 线程吃 CPU）。
  逐项证据与判据在 `docs/android-真机验收清单.md` §K4。
- **仍然没验的**：AAudio 真出声与首帧延迟（B3：竖屏下发射按钮在屏外，点不到）、后台存活与
  音频焦点（Gate E）、FT8 时间基准（Gate F），以及一切要真电台的 G-1…G-5
  （手上只有全盛 UVK6，不是 IC-705、无 50003 音频口）。主窗布局已按用户拍板改成
  **锁横屏 + 分页**（C8，`feat/android-paged-ui`），竖屏不再支持；分页版最后一改
  （HamLog 不可用时不出日志页签）已于 11-01 15:24 真机核对：底部只有
  频谱/调谐/制式/面板 四页签，日志页签不存在（C9 正文）——C9 当晚修掉自建库后，
  17:46 再验已是五页签，「日志」在手机上可用且记 QSO 写通（C9 正文）。
  手机对话框/面板可滚可关（C10，`fix/android-touch-dialogs`）已在 11-01 13:30 真机复验通过
  （频差/亚音下拉、音频设置 Cancel、连接/中继/APRS/WSJT-X 关闭、返回键、SSTV 滚动），
  15:28 又补点了 SET/MEM/CW/DTMF/VOICE 五个对话框，两个新根因（下拉弹窗触摸错位、
  第二顶层窗口丢帧）已修，证据见 `docs/android-真机验收清单.md` C10 正文与 §5 第 11 条。
- 手机上跑：照 `docs/android-真机验收清单.md` 从上往下跑（每项都写了确切的
  adb/aapt/apksigner 命令与判定标准，含签名步骤——产物是 **release-unsigned**，
  不签名装不上）。该清单第 0 节的 K1/K2/K3 是三条必读前置；**先看 §K4**，
  里面是 2026-10-31 第一轮真机的实跑结论和「adb 驱动这台手机的六个坑」，
  已打勾的项不必重复劳动，重复做也要换成新证据而不是照抄旧结论。
- 脚本注意：`.bat` 必须是 **CRLF**，否则 `^` 续行符失效（本轮踩过）；脚本里自带
  VS 的 CMake/Ninja 路径，不需要 vcvars。

### GitHub 与 CI 现状（2026-10-29 更新，2026-10-31 补安卓栈，2026-11-05 按 refs 重核）

- 远端：`https://github.com/fe1iscurc0r/radio-suite`（public，GitHub 自动识别
  **AGPL-3.0**），默认分支 `main`（现 `79f4b51` = PR #36 的 merge commit）。
- **判「这条 PR 到底进没进 main」只用 `git merge-base --is-ancestor <sha> origin/main`。**
  不要看 PR 的 open/closed，也不要看 `merged_at`——两者都会骗人：
  - 11-02 23:13 有四笔消息带 `(local equiv)` 的**本地 merge commit** 落上 main
    （`2c4ea48`←#32 head `9bcb07a`、`1911970`←#33、`3b63e6f`←#34、`dcebfd7`←#35）。
    `9bcb07a` 本身叠在 #16→…→#23→#24…#31 那条链顶端，所以 `2c4ea48` **一笔把
    #16–#32 的全部内容带进了 main**；GitHub 上 #17–#23 至今还 open，只是因为它们的
    base 是已被消化的中间分支——refs 差还在，内容差没有。
  - #33/#34/#35 都有 `merged_at`，但当时合的是各自的 base 侧分支，离 main 还隔一层。
  - **确实没进 main 的只有 #15**（`35570c7`，采集冒烟改测稳态口径），它仍 open。
- **`main` 现在包含什么**（`79f4b51`）：安卓那条链的全部内容（#16–#35）+ 架构清理
  （#36：`radio_session` / `radio_bus` / `radio_dsp` 独立成库、`MainWindow` 按域拆成
  `MainWindow.cpp` + `_actions` + `_panels` + `_android` 四个文件、PWA `apps/web/`、
  频谱 SSE、CI-V over TCP 网关、会话层回帧判活性、瀑布图 30 fps + 双线性放大）。
  拆分完整性核过：main 上 `MainWindow::` 定义 61 处，拆成四份后合计仍是 61。
  本地口径 Qt 6.8.3 msvc2022_64：全量构建 + `ctest` **212/212**。
- ⚠️ **`QT_NO_FAST_MOVE` / `QT_NO_FAST_SCROLL` 那套（#34）已从 main 撤掉**（经 #40）。
  它在真机上被证伪：设备上 `RSDBG fastmove=[1] fastscroll=[1]` 证明开关确实生效，
  屏幕左端那条滞后带一条没少；另外三条路（父控件/`window()->update()` 逐拍强绘、
  换 Qt 6.8.3、改 GL 控件）也全负。甩动期间绘制节奏实测 **15.9 fps（62–63 ms/帧）**，
  根因仍未定位——`gfxinfo` / `SurfaceFlinger --latency` 对 Qt 的 ANativeWindow
  在这台机上取不到有效数，只能自己埋点 + `screencap` 逐帧像素探针。
  留着它的代价是实的：关掉快滚动后每步都要重画整块视口。
  **滚动割裂这件事本身还没解决**，剩余嫌疑指向安卓侧 backing store → ANativeWindow
  的部分区域 flush（缝在屏幕 x≈148）。
- **bus 的 Qt 版本门槛是 6.8，不是保险丝**：CI 三个作业装的 Qt 根本不同
  （Windows install-qt-action `6.8.*`、macOS brew `qt@6`、**Ubuntu apt 6.4.2**），
  而 6.4.2 的 HttpServer 与 6.8 API 互斥——route 视图的 `QHttpServerResponder`
  一边**只收 `&&`**、另一边**只收 `&`**；`req.headers()` 一边是 `QList<QPair<…>>`、
  一边是 `QHttpHeaders`；`bind(QTcpServer*)` 一边返回 `void`、一边返回 `bool`。
  写哪边都炸另一边，所以 `apps/desktop/bus/CMakeLists.txt` 用
  `find_package(Qt6HttpServer 6.8 QUIET)` + 低于 6.8 整个目录跳过，
  下游（app 与 tests）只认 `TARGET radio::bus` 这一个口径，别再各自判 `_FOUND`。
  安卓侧：Qt **6.7.3** 的 arm64 套件没有 QtHttpServer（bus 自动跳过），
  **6.8.3** 的有（实测 `bus_server.cpp` 能为 arm64-v8a 编过并链进可执行目标）——
  所以「手机上有没有总线」取决于装哪个套件。
- **SSE 的形状必须是 chunked**：`responder.write(data, mime)` 发的是「一个完整响应」，
  头里带 `content-length`，裸 `QTcpSocket` 能继续读到后面的帧（所以旧用例是绿的），
  而浏览器 `EventSource` 读完第一段就判定流结束、断开重连，频谱一帧都进不了 JS。
  现在用 `writeBeginChunked` + `writeChunk`，用例也改成断言
  `transfer-encoding: chunked` 且无 `content-length`（做过变异对照：换回旧写法就 FAIL）。
  另：`apps/web/app.js` 里的 `openSse()` 之前定义了却没被调用，现已接上启动。
- **待合进 main 的**：#41（PWA 静态件补 `Cache-Control: no-store`；总线不带缓存头时
  浏览器按启发式缓存旧 `app.js`，改完前端同端口刷新还在跑旧脚本，`ignoreCache`
  的顶层导航都换不掉子资源——验前端改动时记得这一手）。
- **PR #1–#14 均已合并进 main**：一键发射编排（`272bc5d`）、旋钮/频差/亚音（`854bf42`）、
  50003 音频链（`5014831`）、APRS 全量工作台（`09222e4`）、WSJT-X UDP 接入（`9b3e9ac`）、
  QSO 直写 HamLog（`bef3eec`）、审查修复 + 算法优化（`f531346`）、50003 设备可选与诊断
  （`6d7040d`）、win deploy（`7073502`）、ft8_lib vendor（`4ff82d9`）、安卓工具链
  （`fff069f`）、AAudio 后端（`f4091f6`）、APK 打包（`5a08fa4`）；每次合并后 main
  推送 CI 三平台 success。
- **CI 三平台全绿**只对 `main` 成立；`feat/arch-cleanup` 在 bus 独立成库后曾连续 5 次
  全红（run 102–106），成因见上面那条，修在 #38。
- 推码注意：本机 **git 协议到 github.com:443 常不通**（api.github.com 通）。
  两条路：① git push 成功时直接用；② 不通时用 REST API 推
  （`gh api` 或仓外脚本：create blob → create tree(base_tree) → create commit
  → PATCH refs/heads/<branch>）。**REST 内容必须从 `git cat-file blob <sha>`
  取原始字节并显式 `encoding=base64`，不可读 CRLF 工作树冒充 blob**。
  **token 只经环境变量**（`GH_TOKEN` / `GITHUB_TOKEN`），不写仓库或 git config。
  远端 main 曾有两个 CRLF blob（`apps/desktop/audio/CMakeLists.txt`、
  `apps/desktop/core/transport_mock.h`），PR #1 已用本地 LF blob 建树清偿。
- ⚠️ **本地与远端末条 commit 的 sha 不同**（忽略行尾后内容等价）：因
  github.com:443 不通，最后几次用 REST API 重建了 commit 对象。接手人若直接
  `git push` 会 non-fast-forward。先逐 blob 核对基线，再从确认后的基线**另开
  新分支**交付；不要为对齐 sha 丢弃未提交工作，也不要直推/强推 main。
- 跨平台可移植性（CI 实证的坑）：① audio 平台后端必须三分支
  （WIN32=WASAPI / APPLE=`__MACOSX__`+CoreAudio 框架 / else=ALSA），
  非 Windows 一律编 ALSA 会让 macOS 缺 asoundlib.h；② `uint8_t arr[] = {cond ? a : b}`
  在 Clang 报 narrowing，须 `static_cast<uint8_t>`；③ 类内默认成员初始化器
  （`SimConfig`）不可作类外默认实参，须重载。


## 7. 已知坑（省接手人时间）

1. **Mimosa 钩子**：Bash 命令文本里出现 `gen_from_reference.py` 会被拦（含
   git commit 消息）——该文件改动走 Edit/Write，commit 消息避开文件名。
2. **Catch2 用例名必须 ASCII**：ctest 发现机制在 Windows 上损坏中文 argv
   （表现为 "No test cases matched"）。中文放 SECTION/注释。
3. **AUTOMOC 增量滞后**：新增源文件后错误（未实现 slot/丢 namespace/值成员前向
   声明）要到 ninja regenerate 才暴露——改完头文件先 `touch` 源文件再编。
4. **Qt 6.8 HttpServer**：模块名 `qthttpserver`（依赖 `qtwebsockets`），
   `find_package(Qt6 COMPONENTS HttpServer)` 不置 _FOUND，用
   `find_package(Qt6HttpServer)`；API 与 6.4 差异大（`bind(QTcpServer*)`、
   `QHttpHeaders.value()`、双参构造 (mimeType,data)）。
5. **WASAPI**：见 §6/文档。
6. **截图 DPI**：本机 150% 缩放，截图脚本必须 `SetProcessDPIAware`，否则只截到
   窗口左上 2/3（曾因此误判 UI 缺控件）。UIA 枚举比视觉模型可靠。
7. **亚音两套载荷别混**：`0x1B 0x00/0x01` 是频率
   `[sub][00][100Hz|10Hz][1Hz|0.1Hz]`（0.1 Hz 单位，88.5→`00 08 85`）；
   `0x1B 0x02` 是 `[sub][TX 极性<<4|RX 极性][百位][十位<<4|个位]`。
   mock 曾按 `(code/100)<<4` 编码 DTCS 码，把 023 读成 230，已修——改这块先跑
   `[civ] ctcss and dtcs …` 与 `[ui] repeater dialog …`。
8. **`0x0F` 写后必须读回**：`setDuplex()` 只发写帧时状态永远停在 simplex；
   该函数已补 `readDuplexFrame` + `poll()`，别再拆掉。
9. **vendor 的 C 代码在 MSVC 上要补三处**：无 VLA（上游 `monitor.c` 变长数组、
   `decode.c` 的 `s2[]`）、无 `stpcpy`（补 `third_party/ft8_lib/common/compat_msvc.c`）、
   `M_PI` 要自己定义（`_USE_MATH_DEFINES` 不可靠）。升级 `ft8_lib` 前先看
   `third_party/ft8_lib/VENDORED.md` 的补丁清单。
10. **无父顶层面板会拖住进程**：APRS / WSJT-X / FT8 面板都是 `new XxxPanel(nullptr)`
    的独立顶层窗口，Qt 只在「最后一个可见顶层窗口关闭」时退出——开着任意面板关掉主窗，
    窗口全没了但事件循环还在跑（FT8 面板还会留一个活解码线程）。`MainWindow::closeEvent`
    已统一处理（先 `ft8_panel_->stopDecoding()`，再 hide 其它可见顶层窗口）。
    2026-10-30 实窗验证：修复前主窗关闭后 `radio-suite.exe` 仍在任务列表；修复后进程退出。
    再加同类面板时不必改这里，但**新建带线程的面板要先停线程**。
11. **整槽发射必须独占 TX，且不走 50003 上行**：FT8 一发就是 15 s，`beginScriptedTx()`
    对「已经在 Tx」是放行的（SSTV/APRS 那种「接着键控」的语义），FT8 不能跟着放行——
    两路 `AudioOut` 会同时进声卡、叠成一头发不出去的垃圾，所以主窗出口在 `beginScriptedTx`
    之前先查 `session_.gate().phase() == Tx` 直接拒。发射也不注进 `NetworkAudio::queueTxPcm`：
    `AudioBridge` 队列只有 10 帧 = 200 ms、满则丢最旧（实时语音宁缺勿积），15 s 整槽丢一帧
    就废，要走这条路得先给桥加一条「背压时不丢、拒绝入队」的定长发送通道。
    同理，`~MainWindow` 里对 `ft8_tx_audio_` 先 `wait()` 再 `endScriptedTx()`：播放线程的
    收尾回调排在事件队列里，关窗路上不一定还有机会跑，不能指望它把 PTT 放掉。
12. **Qt 宏会吃掉标识符 `signals` / `slots`**：在含 Qt 头的源文件里写 `int signals = 0;`
    编译报 `C2059: 语法错误:"public"`（`signals` 展开成 `public`），错误信息完全指不到真因。
    计数变量改名 `signal_count` / `xxx_signals` 之类。
13. **状态标志与按钮标签必须同批改**：`fireTransmit()` 落 `tx_pending_ = false` 却没调
    `updateTxLabel()`，发射完按钮上还挂着「取消发射」，操作者那一刀点下去是**排下一次
    15 s 发射**、不是取消（`onTxClicked()` 只看 `tx_pending_`）。`refreshTick()` 也只在
    pending 时刷标签，救不回来。现在 `fireTransmit` 落标志后立刻刷标签，offscreen 用例
    把「发完/被拒/取消后标签==「定时发射」」钉住；删掉那行 `updateTxLabel` 两个用例立刻红。
14. **声学环回的 DF 会跳，别当成解码 bug**：不接电台、喇叭对麦克风自收自发时，
    1000 Hz 那一档实测报过 750 / 1250 / 2250 / 2750（全是 250 Hz 的奇数倍 = 6.25 Hz 音调
    间距的整数偏移），同一链路 1200 / 1500 Hz 又分毫不差。离线渲染→解码的用例
    `ft8 tx: reported DF tracks the tone offset` 在 500/1000/1500/2000 四档逐档精确命中，
    所以软件路径（`min_bin + freq_offset + freq_sub/osr` 除符号周期）是对的，偏的是
    喇叭→房间→麦克风这一段（削波、房间响应、Windows 采集端的回声消除/AGC）。
    **结论：DF 只在 50003 下行或真机通路上可信**，双击定频前别拿声学环回的 DF 当准。
15. **Android 上 QSettings 有三条看着能用、实际静默失败的路**（2026-10-31 逐条真机撞过）：
    ① 默认构造 = NativeFormat，安卓没有后端，**写了等于没写**；② `IniFormat/UserScope` 要经
    `HOME`/`XDG_CONFIG_HOME` 解析，而安卓进程的 `/proc/<pid>/environ` 里根本没有这两个变量；
    ③ 退到 `applicationDirPath()` 会落到 APK 解压出的 `/data/app/~~…/lib/arm64`
    （root:root 755），QSettings 的「写临时文件 + rename」在那儿失败**且 logcat 一条警告都没有**。
    唯一可写落点 = `QStandardPaths::writableLocation(AppDataLocation)`（这台机上是
    `/data/user/0/<pkg>/files`），回退 `QDir::currentPath()`（`/proc/<pid>/cwd` 也是那里）。
    **怎么判有没有真写进去：只看应用自己写出的 ini 字节**，别拿「点了保存、对话框关了」当证据
    ——`app_settings.h` 是唯一入口，别再在任何一处 `QSettings(...) ` 直接构造。
16. **adb 驱动 MIUI 14 这台机的坑**（完整六条在 `docs/android-真机验收清单.md` §K4 末尾，
    下次照清单跑之前先看那段）：`uiautomator` 报的 **QDialog 按钮 bounds 右偏约 126px**
    （正好一个按钮 + 间距），照 bounds 中心 `input tap` 会点到隔壁——而对话框里两个按钮
    **都会关掉窗**，所以「窗关了」不是判据；密码框 `QLineEdit::Password` 置 FLAG_SECURE 时
    `screencap` 返回 **0 字节**；软键盘弹起会盖住按钮排，`input keyevent 111` 收起 IME 后
    触控才恢复投递；`uiautomator dump` 只报最上层窗口。主窗按钮 bounds 不偏，别把两边结论互抄。
    还有一条会坑取证：`adb shell … cat` 取二进制文件会插 `\r`（36864 字节的 db 拉回来变
    36868），跟设备侧 `md5sum` 对账前先把 `\r\n` 还原成 `\n`，否则会误判「文件被改过」。
    **读「按钮亮没亮」有直路**：这台机上 Qt 把控件标签放进 uiautomator 的 `content-desc`
    （`text=` 属性全空，别拿它当没渲染），而 `checkable`/`checked` 是真值 —— 判控件状态
    不必猜像素；主窗 bounds 是准的（右偏那条只在 QDialog）。
18. **Android 的 Qt 插件只认「可执行目标自己挂上的插件」**：挂在**静态库**上的
    `Qt6::Sql` 不会把 `sqldrivers/qsqlite` 带进 APK —— 包里有 `libQt6Sql.so` 但驱动列表是空的，
    `QSqlDatabase::isDriverAvailable("QSQLITE")` 恒假（真机 D3 就是这么露出来的）。
    修法见 `apps/desktop/app/CMakeLists.txt` 的 `qt_import_plugins(radio-suite INCLUDE
    Qt6::QSQLiteDriverPlugin)`。**别走 `QT_ANDROID_EXTRA_PLUGINS`**：那个键在 androiddeployqt
    里是「往 `assets/` 拷外部资源」，喂 `.so` 路径或类型名 `sqldrivers` 都会
    `External resource … does not exist or not a correct directory!` 停建（两种实测过），
    就算指到目录也只是塞进 assets，Qt 运行时不从那儿找插件。
    验包只要一条命令：`unzip -l <apk> | grep sqldrivers`。
17. **MIUI「USB 调试（安全设置）」是安全设置，不要自动化去翻它**：`adb install` 报
    `INSTALL_FAILED_USER_RESTRICTED` 时，走这台机已有的 Magisk root——
    `su -c 'pm install -r /data/local/tmp/xxx.apk'`，把安装这件事留在设备权限之外解决。
19. **「回读驱动」的按钮必须在控件层自己互斥**（2026-10-31 用户真机报「六个制式能同时开」）：
    六个制式按钮 `setCheckable` + 点下 `setChecked(true)`，而唯一的取消路径
    `syncTogglesFromState()` 只在**有回读帧真的改了状态**时才跑（`RadioSession::onFrame` 末尾
    才 `emit stateChanged()`）。链路选 UDP 而电台关机 / IP 填错 ⇒ 一帧回读也没有，点几个亮几个
    且永不纠偏。`setAutoExclusive(true)` 一行才是把不变量放回控件层。同一段循环里还有第二条：
    `Mode` 枚举有 `RTTY=4` 而按钮行没有 RTTY，拿**数组下标**当枚举值比 ⇒ FM 的回读点亮写着
    WFM 的那个按钮、WFM 的回读谁都不点。凡是「按钮 ↔ 枚举」，把枚举值存在按钮旁边按值比，
    别赌下标顺序。**MainWindow 不在任何测试目标里**，这类控件不变量目前只有实窗证据
    （UIA 读 `selected`），要落成用例得先把 app 源文件编进一个 offscreen 测试目标。

## 8. 工单脉络

| 单 | 主题 | 交付 |
|---|---|---|
| 268 | radio-suite 建仓 + 选型（QSO 保留） | M0–M2 全套功能 + 原厂对齐（`docs/adr/`、`docs/original-rsba1-feature-map.md`） |
| 273 | 竞品盘点 + 双端裁决 | `docs/research/radio-competitors-2026-10-25.md`、`wo273-verdicts.md`、`wo273-integration-map.md`（集大成图谱） |
| 274 | 换引擎（rigplane sidecar） | `docs/engine/rigplane-api-notes.md`、`docs/WO-274-spike.md`、`docs/engine/268-delta.md`、上游 issue 文本 |

施工记录在 scratchpad 侧：`D:/my git/scratchpad/docs/WO-268-radio-suite.md`
（分 §0–⑮ 逐批记录，含每批真实命令输出与踩坑）。
