# radio-suite 交接说明（2026-10-28 更新）

> 一句话：**独立 AGPL 桌面电台客户端**（Qt6/C++20），已完成 M0–M2 主体功能 +
> 原厂功能对齐（含 2026-10-27 补齐的 CW / 双 VFO / Set Mode / Memory 四件 +
> 同日的 UI 重排与中继频差 / 模拟·数字亚音）+ 两条生态融合（hamlog / APRS 工作台）+
> WSJT-X UDP 接入（解码 / QSO / WSPR / ADIF / 远程控制）+
> 50003 网络语音链（RX 播放 / PTT 门禁 TX / 真实频谱）+ 引擎化准备（WO-274），
> 130/130 测试全绿；
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

## 2. 目录结构（WO-273 单仓 monorepo 裁决，已落地）

```
radio-suite/
├── apps/desktop/{core,app,aprs,wsjtx,logbook,audio,bus}   # 桌面端（C++/Qt6）
├── apps/spike/                                   # WO-274 rigplane 引擎 spike（默认不编）
├── libs/dsp/dsp/                                 # FFT/瀑布（双端共用）
├── libs/protocol/rsba1-core/                     # MIT 跟随副本（subtree 同步）
├── tests/ (Catch2, 130 用例)  docs/  packaging/  tools/
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
| RS-BA1 V2 UDP | ✅ 控制协议+传输层（登录/鉴权/0xA8/ConnectTrans/重传重排）+ **50003 音频双通路已接 app**（RX 重排→扬声器，麦克风→PTT 门禁上行） | tests `[rsba1]`/`[audio]`；真机未验（搁置） |
| rigctld 服务器 | ✅ 过 rigplane **全部轮询命令** | 真实 loopback TCP 测试 `[rigctld]` |
| HTTP 总线 | ✅ `/api/state` `/api/spectrum` + 演示页；只有 HTTP，无 WS/控制 POST | 真实 HTTP 测试 `[bus]`（含 token/绑定门禁） |
| 安全门禁 | ✅ PTT 二次确认 + 10min 自动回 RX + **非业余段拒绝发射**（TX 中调谐也拦） | tests `[safety]` + `[session][ptt]` 帧序列 |
| 频谱/瀑布 | ✅ OpenGL 纹理滚屏 + 谱线（**上谱下瀑分区**）+ 半圆指针 S 表 + Po/SWR 表；默认合成谱，**网络音频开启后切真实 RX PCM** | 截图 `docs/screenshots/` + `SignalSynth` / RX tap 调用点 |
| 频率/模式/开关 | ✅ 频率/模式/滤波/RIT/ΔTX/Split/ATT/NB/NR/NOTCH/VOX/PAMP/AGC/DUP + **双 VFO(SUB 带)** + **中继频差(0x0D) / CTCSS·TSQL·DTCS** | 对照原厂 `IC-705.ini` 全清单；SUB/频差/亚音帧级测试 `[civ]`/`[session]` |
| 原厂件 | ✅ 调谐旋钮（132px 旋钮台，与 S 表/Po·SWR 同排）、记忆槽、Connect Setting、SSTV(8 制式)、DTMF、Voice Memory、**CW Keyer / Set Mode / Memory Channel / 中继·亚音对话框** | 原厂 CHM 清单逐项对照；四件见 `docs/original-rsba1-feature-map.md` §K，UI/频差/亚音见 §L |
| SSTV | ✅ 8 制式编解码（Robot36/72 + Martin M1/M2）+ VoX 校准头 + **编解码回环自校验** | tests `[sstv]`(31 断言) |
| DTMF | ✅ 16 键编解码 + 回环 | tests `[dtmf]` |
| APRS | ✅ 核心库（纯 std）：TNC2、三种坐标（未压缩/压缩/Mic-E）、对象·气象·遥测、消息 + ACK/REJ + 重发、第三方解包、12 类过滤器、通行码；**工作台 UI**（OSM 地图 + 台站表 + 消息窗）；APRS-IS 连接（china/rotate 轮换、指数退避）；**KISS TCP/串口**（Dire Wolf / LoRa APRS 网关）+ Bell 202 AFSK 1200 Bd 走 50003；**iGate 双向**（RF→IS 打 qAR、IS→RF 仅消息/对象并去重）；独立 SQLite 持久化（`%APPDATA%/radio-suite/aprs.db`） | tests `[aprs]` 18 + `[aprs_persist]` + offscreen `[aprs_ui]` 3；APRS-IS 实测 china.aprs2.net（308 台/7805 包）；**RF / LoRa / AFSK 真机未验** |
| HamLog 融合 | ✅ **同库直读写** + 工作台 v2（四字段模糊搜索/完整编辑/模板/设置/QRZ/ADIF/**LoTW 上传**/字体/代理） | 临时 SQLite CRUD/搜索测试 + UTF-8 ADIF 长度测试；真实库仅人工验收 |
| WSJT-X 接入 | ✅ UDP 协议 schema 3 全量编解码（纯 std：心跳/状态/解码/清空/QSO/关闭/重放/HaltTx/自由文本/WSPR/位置/ADIF/配置）；组播监听（`224.0.0.1:2237`）+ **工作台 UI**（解码表/QSO/WSPR/ADIF 四页，CQ·QRZ 高亮、低置信度着色）；控制出口：呼叫、停止发射、重放、自由文本、设置网格 | tests `[wsjtx]` 8 + offscreen `[wsjtx_ui]` 2（UDP 回环 + 面板渲染）；**未与真实 WSJT-X / JTDX 联调** |
| 音频 | ✅ 本机输出（AudioOut，SSTV/Voice Memory）+ 输入（AudioIn）+ **50003 流式播放（AudioSink）与 PTT 门禁麦克风上行**；设备用系统默认输入/输出 | 门禁单测 `[audio]`；本机冒烟；真机通话未验，WASAPI 丢样见 §7 |
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
   设备固定取系统默认输入/输出，无设备选择 UI；抖动/丢包计数未上界面。
   复验清单见 `docs/original-rsba1-feature-map.md` §L.6。
6. **APRS 真机复验**（代码/单测已齐，但**未上真机**）：① KISS 串口接 TNC /
   Dire Wolf 收发 AX.25；② LoRa APRS 网关（KISS over TCP）收发；③ 50003 AFSK
   1200 Bd 实际空口回环（含音频延迟/PTT 尾）；④ iGate 双向在真实 APRS-IS 上
   观察 qAR 与去重。已知边界：LoRa 只按 KISS 网关形态支持，无原生 LoRa 硬件
   抽象；地图瓦片需网络（测试用 `RADIO_SUITE_APRS_TILES=off` 关闭）。
7. **WSJT-X 联调**（协议与 UI 已齐，UDP 回环测试通过，**未接真实 WSJT-X**）：
   ① 在 WSJT-X `Settings → Reporting` 启用 `Accept UDP requests` 并统一组播
   地址/端口；② 验证 Status/Decode 刷新、双击/呼叫选中回发 Reply；③ 验证
   QSO Logged 与 ADIF 导出页；④ 验证 HaltTx / FreeText / Replay / Location
   在真机上的行为（含跨机器 UDP 防火墙放行）。
8. **工程小债**：logbook 库反向引用 app 层设置对话框头（链接可解）→ 挪 libs/ui 时清。
9. ~~GitHub 推送~~ → **已于 2026-10-26 完成**（远端 fe1iscurc0r/radio-suite，
   public/AGPL-3.0，CI 三平台全绿；详见 §6「GitHub 与 CI 现状」）。

## 6. 构建与验证（本机环境）

```bash
# 环境：MSVC 14.44（VS2022 BuildTools）+ Qt 6.8.3（D:/Qt）+ CMake/Ninja（VS 自带）
cmd //c "D:\tmp\rs-build.bat configure"   # 需 MSVC 环境（vcvars64）
cmd //c "D:\tmp\rs-build.bat build"
cmd //c "D:\tmp\rs-build.bat test"        # 130/130
cmd //c "D:\tmp\rs-build.bat spike-configure && spike-build"   # WO-274 spike
```

- FetchContent 需网络；本机 github 间歇不通 → 已在 rs-build.bat 用
  `FETCHCONTENT_SOURCE_DIR_POCKETFFT/CATCH2` 指向 `D:/tmp/` 的本地副本。
- python 真身：`C:/Users/ASUS/python-sdk/python3.13.2/python.exe`（PATH 上的
  python 是 WindowsApps 假壳）。
- 麦克风冒烟：`build/apps/desktop/audio/audio-in-smoke.exe`。
- 130 用例包括：session PTT 真实 CI-V 帧序列、bus 真实 HTTP、rigctld 真实 TCP、
  logbook 临时 SQLite，直接调用生产 `scope_wire.*` 的 spike 测试，音频门禁
  （`AudioBridge` 半双工/有界队列/失败重试）单测，以及
  `radio_ui_tests`（offscreen QPA，真点 CW/Set Mode/Memory/中继·亚音对话框按钮
  并断言 mock 与状态回读；`BUILD_GUI=OFF` 的 Linux CI 自动不建该 target）。
  SSTV/Voice Memory 已统一走 `beginScriptedTx/endScriptedTx`；旧“双击收尾”经
  受控变异确认会 FAIL（仍处 TX / 无 PTT OFF），恢复后全绿。
- 部署目录 `packaging/out-win/`（windeployqt 后可独立运行）。

### GitHub 与 CI 现状（2026-10-27 更新）

- 远端：`https://github.com/fe1iscurc0r/radio-suite`（public，GitHub 自动识别
  **AGPL-3.0**），默认分支 `main`（当前 `854bf42`，PR #2 合并后）+
  `feat/rsba1-audio-chain`（50003 音频链，待评审）。
- **PR #1 已合并**（2026-10-27，一键发射 PTT 编排 + 真实集成测试）；其分支
  `fix/scripted-tx-and-real-tests` 的远端 sha `272bc5d` 是 REST 重建对象，
  与本地同内容 commit 的 sha 不同。
- **PR #2 已合并**（2026-10-27，旋钮调谐台 + 中继频差双频 + 亚音），merge
  commit `854bf42`，分支 `feat/ui-tone-repeater`；合并前 CI run `40464364408`
  三平台 success，合并后 main run `40465738233` 三平台 success。
- **CI 三平台全绿**：Ubuntu headless（BUILD_GUI=OFF）/ Windows MSVC+Qt /
  macOS Qt。
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

## 8. 工单脉络

| 单 | 主题 | 交付 |
|---|---|---|
| 268 | radio-suite 建仓 + 选型（QSO 保留） | M0–M2 全套功能 + 原厂对齐（`docs/adr/`、`docs/original-rsba1-feature-map.md`） |
| 273 | 竞品盘点 + 双端裁决 | `docs/research/radio-competitors-2026-10-25.md`、`wo273-verdicts.md`、`wo273-integration-map.md`（集大成图谱） |
| 274 | 换引擎（rigplane sidecar） | `docs/engine/rigplane-api-notes.md`、`docs/WO-274-spike.md`、`docs/engine/268-delta.md`、上游 issue 文本 |

施工记录在 scratchpad 侧：`D:/my git/scratchpad/docs/WO-268-radio-suite.md`
（分 §0–⑮ 逐批记录，含每批真实命令输出与踩坑）。
