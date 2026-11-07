# 准备：接入 MCP + 手机端 UI 连贯度（2026-11-05）

> 这是**准备**文档：核事实、给方案、列任务、标出要拍的问题。本文件不改任何产品代码。
> 所有「现状」都是对着代码/上游仓核过的，带文件行号；推测都标了「推定」。

---

## 第一部分 接入 MCP

### 1.1 我们已经有什么（不用新建的部分）

| 资产 | 位置 | 对 MCP 的意义 |
|---|---|---|
| HTTP 总线 `/api/state` `/api/spectrum` `/api/spectrum/stream`(SSE) `/api/control`(POST) `/api/qso` `/api/qso.adif` + PWA 静态件 | `apps/desktop/bus/bus_server.cpp` | 这就是工具面的现成后端，**一个 C++ 接口都不用新写** |
| token 门禁 + 「非环回绑定必须带 token」 | 同上 `authorized()` / `start()` 开头 | 远程暴露的鉴权边界已经在了 |
| rigctld 服务器（过 rigplane 全部轮询命令） | `apps/desktop/bus/rigctld_server.cpp` | 给 WSJT-X/fldigi 那类老客户端；MCP 走总线，不走它 |
| 会话唯一出口 + SafetyGate（PTT 二次确认、非业余段拒发、10min 自动回 RX） | `apps/desktop/session/`、`core/safety.*` | **MCP 绝不能绕过的东西**，见 1.5 |
| CI-V over TCP 网关 + 串口对端 | `core/transport_tcp_gateway.*`、#43 | 电台侧从 mock 换成真 CAT 线，MCP 的控制链才有真意义 |

姊妹仓 **rsba1-core 已经有一份能跑的 MCP server**（本仓最好的形态参考，不是代码来源）：

- `D:/my git/rs-ba1-reverse/src/rsba1/mcp/server.py`（240 行，`fastmcp`），`__main__.py` 71 行；
- 6 个工具：`read_freq` `read_mode` `read_smeter` `set_freq` `ptt` `get_status`；
- 起法 `PYTHONPATH=src python -m rsba1.mcp [--transport stdio|sse] [--to 0xA4] [--from 0x00]`；
- 接入说明 `docs/MCP_CLIENT.md`，冒烟 `tools/mcp/mcp_smoke_test.py`；
- 底座是 **Mailslot ExecCmd 桥**（走 RemoteUty.exe），不是总线。

⚠️ 别照抄的两点：① 它 `--transport sse` 用的是**已废弃的 HTTP+SSE 传输**（见 1.2）；
② 它文档里的示例端口 **8765 与本仓总线默认端口撞**，同机跑会互相抢。

### 1.2 协议现状（按官方核过，不是博客）

官方规范页 `modelcontextprotocol.io/specification/2026-07-28/changelog` 原文三条：

- 「Reclassify the HTTP+SSE transport … as Deprecated」（SEP-2596）；
- 「Remove protocol-level sessions and the `Mcp-Session-Id` header」；
- 「Make MCP stateless: remove the `initialize`/`notifications/initialized` handshake」。

所以 2026-07-28 这一版（前序 2025-11-25 / 2025-06-18）的实操结论：

| 传输 | 定位 | 我们怎么用 |
|---|---|---|
| **stdio** | 标准，本地子进程 | 第一版就做它：宿主（lumo/scratchpad）把适配器当子进程拉起 |
| **Streamable HTTP** | 标准，远程/多客户端 | 第二阶段；无状态核心 = 任意实例能处理任意请求，不用粘性路由 |
| 旧 HTTP+SSE | **已弃用** | 不做，也不在文档里教 |

次要来源（与官方一致，仅作旁证，不作依据）：
[MCP 协议学习笔记 2026 版](https://m.blog.csdn.net/qq528454387/article/details/168874166)、
[stdio vs Streamable HTTP 企业部署](https://www.truefoundry.com/blog/mcp-stdio-vs-streamable-http-enterprise)、
[MCP 传输机制第 3 课](https://blog.csdn.net/weixin_40192882/article/details/167222743)、
[MCP 2026 规范变化](https://developer.aliyun.com/article/1770341)。

新版的硬约束（写进适配器设计）：请求头要带 `MCP-Protocol-Version` / `Mcp-Method`，
工具调用还要带 `Mcp-Name`，且头里的值必须与正文一致；对端认新版但不认这个版本时回
`-32022 UnsupportedProtocolVersion`，客户端**要按规范协商版本**而不是退回旧握手；
长连接推送换成 `ttlMs` / `cacheScope` 缓存元数据。

### 1.3 三条路，推荐第三条

| 方案 | 说明 | 判定 |
|---|---|---|
| A. C++ 里直接做 MCP server（并进 `radio_bus`） | Qt 无官方 MCP SDK，要自己写 JSON-RPC framing + Streamable HTTP + 版本协商；协议演进被 C++ 发布节奏绑死 | **否** |
| B. Python 适配器直连电台（照 rsba1-core 的 Mailslot 路子） | 绕开 `RadioSession` = 绕开门禁、状态单源、活性判定、QSO 落库——正好把我们唯一差异点拆掉 | **否** |
| C. **Python 薄适配器 → 只吃本仓 HTTP 总线** | 门禁/状态/日志仍在 C++ 单源；适配器是纯转写（约 100–200 行）；形态与 rsba1-core 一致，宿主接法不变；总线契约已有 `[bus]` 用例兜底 | **✔ 推荐** |

放哪：`tools/mcp_radio/`（`tools/` 现在已被 CMake 排除在 ANDROID 之外，纯 Python 不参与交叉编译）。

### 1.4 第一版工具面（9 个，全部映射到已有端点，不新增总线能力）

| 工具 | 总线调用 | 备注 |
|---|---|---|
| `get_state` | `GET /api/state` | 频率/制式/滤波/PTT/S 表/衰减…一锅端，字段就是总线的字段 |
| `get_spectrum` | `GET /api/spectrum` | 快照；**流式先不做**（SSE 是长连接，与无状态 MCP 的映射要单独设计） |
| `tune` | `POST /api/control {action:"freq"}` | 走会话唯一出口，非业余段照样被拦 |
| `set_mode` | `POST /api/control` | 制式互斥逻辑在会话层，不在适配器重复一遍 |
| `set_toggle` | `POST /api/control` | ATT/NB/NR/VOX/RIT/ΔTX/SPLIT… |
| `ptt` | `POST /api/control {action:"ptt_on"}` | **两步语义原样透出**，见 1.5 |
| `log_qso` | `POST /api/qso` | 落总线内存队列，再由 HamLog 侧导出 |
| `export_adif` | `GET /api/qso.adif` | 只读 |
| `link_health` | `GET /api/state` + `framesDelivered`/活性 | 让 agent 自己看得见「这条链到底通不通」 |

总线现在**没有**的能力（频谱历史、记忆信道读写、CW 文本发射、iGate 配置）先不进工具面——
要就先加总线和用例，再加 MCP 工具，别在适配器里开第二条实现。

### 1.5 安全边界（写死，不许适配器打折）

1. **发射类工具不许"帮用户确认第二次"**。总线的 `ptt_on` 第一次返回 `confirm_pending`、
   第二次才真发射；适配器必须把 `confirm_pending` 原样回给模型，由显式 `confirm=true` 参数
   再走第二次。SafetyGate 的 10min 自动回 RX 与非业余段拒绝**一律保留**。
2. 适配器默认只连 `127.0.0.1`；要连远端总线必须由用户显式给 host，且总线那边
   非环回绑定本来就强制 token。
3. token 只走环境变量（`RADIO_BUS_TOKEN`），不落盘、不进命令行参数（会进 shell 历史与
   `ps`）、不写进仓库。**不打印 token 明文**。
4. stdio 传输的日志一律写 stderr——官方规范明确 stdout 的一行 `print()` 就能破坏协议帧。
5. HTTP 传输的 MCP 端点**默认无鉴权**（官方原话），所以第二阶段上 HTTP 时，鉴权要么复用
   总线 token，要么明确写「仅限本机/内网，出网前自加网关」，不能拿「能 curl 通」当生产就绪。
6. 台账页/自检要能显示「MCP 是否在连着」（总线已有 SSE/HTTP 客户端计数，可复用同一口径）。

### 1.6 验收

- **冒烟**：照 rsba1-core 的 `tools/mcp/mcp_smoke_test.py` 形状做一份
  （起 stdio → `tools/list` → 调 `get_state` → 断言字段齐全 → 调 `tune` → 再读回）。
- **契约**：总线侧 `[bus]` 用例已经覆盖端点与 token 门禁；适配器侧加一条 Python 用例，
  CI 缺 `fastmcp` 就 skip（别把 CI 绑在 Python 依赖上）。
- **门禁回归**：必须有一条用例证明「适配器不能一次调用就发射」和「非业余段被拦」，
  否则 1.5 只是愿望。
- **真机/真电台**：MCP 与 PWA 同时连着同一条总线，验并发与状态一致（等 IC-705 到手）。

### 1.7 任务拆分（点头才开工）

- **M0** 骨架：`tools/mcp_radio/` + README + stdio 起进程 + `tools/list` 冒烟。
- **M1** 只读三件：`get_state` / `get_spectrum` / `link_health`（零风险，能独立验收）。
- **M2** 写工具 + 两步 PTT + 门禁回归用例。
- **M3** Streamable HTTP 传输 + 版本协商（`-32022` 路径）+ 鉴权口径。
- **M4** 与 rsba1-core 那份的关系：合并成一个「电台 MCP」门面（推荐，宿主只配一个 server），
  还是并存两个（底座不同：Mailslot 桥 vs 本仓总线）。**这条要你拍。**

---

## 第二部分 手机端 UI 连贯度

### 2.1 现状（对着代码核的，带行号）

手机 UI 在 `apps/desktop/app/MainWindow_android.cpp:60-230`：顶部 LCD 带 + 状态行 + PTT +
连接按钮，中间 `pages_` 五页，底部页签。问题按影响排序：

1. **面板页 10 个入口里 7 个是模态 `exec()`**（`MainWindow_panels.cpp:421-445`），
   只有 APRS/WSJT-X/FT8 三个是 modeless 顶层（`:148 wb->show()`）。手机上模态 = 主界面整块冻住、
   系统返回键语义不一致、还得靠 `touch_dialog` 的 `fitDialog`/`addCloseBar`/`showInfo`
   给每个对话框补「能滚 + 有关闭条」。**形态本身是不连贯的源头，补丁是它的利息。**
2. **只有 2/5 页可滚**：调谐页 `:146`、制式页 `:161` 走 `wrapScroll`；
   频谱 `:133`、面板 `:177`、日志 `:197` 不走。同样高度下有的页被裁、有的能滚，
   而且 #101 那条「滚动左端割裂」只在可滚页出现——**能不能滚是随机的**。
3. **主屏不能完成主任务**：频谱页的瀑布图零交互（`WaterfallWidget.cpp` 里
   `grep mouse|wheel|gesture` 零命中），调频必须切到「调谐」页。
4. **页签标签与页是两套代码**：标签是硬编码 `QStringList{"频谱","调谐","制式","面板"}`（`:206`），
   页却在 5 个独立块里 `addWidget`（`:133/146/161/177/197`），日志页靠 `have_log_page` 补丁对齐。
   加一页忘改标签就 index 错位——**这是会静默出错的耦合**。
5. **两端不一致**：刻度与光标读数刚加到桌面（PR #44 的 `wf_axis_label`），手机页 1 没有。
6. **常驻状态太少**：页 1 只有 LCD（`:74`）+ 状态行（`:80`）；制式/滤波/ATT/NB/NR/双 VFO
   散在页 2/3，主屏看不到「现在到底是什么配置」。

### 2.2 可以参考的项目（设计参考，不是代码来源）

| 项目 | 拿什么 | 红线 |
|---|---|---|
| **Decodium3-Android** | 六行单屏：频率+制式芯片+S 表 / 瀑布 / 解码列表 / 横滑功能带 / 动作行 / 状态行 | **未声明许可证 → 只当设计参考，一行代码都不抄** |
| **OpenWebRX / wfweb** | 频谱即交互面（点/拖=调谐）；Web 与原生同一套信息架构 | wfweb 是 GPL-3.0，若要整文件移植按 AGPLv3 §13 进 `libs/**/gpl-origin/` 并标注 |
| **SDR++** | 主屏 + 抽屉式设置，不用模态弹窗——正是 2.1 第 1 条的解 | 设计参考 |
| **Look4Sat** | 卫星页 + 双 VFO 编排（#99 的参考） | 设计参考 |
| **rigplane 的 web 四皮肤** | 同一状态多皮肤的信息架构（`wo273-integration-map.md` §2.3 已记） | MIT，可移植但要标出处 |

### 2.3 目标形态（建议）

一页主屏搞定主任务，其余降级为抽屉/子区：

```
┌ 频率 + 制式芯片 + S 表          ┐  ← 常驻，任何操作后都看得到结果
│ 瀑布（点=调谐 / 拖=连续调 / 刻度+光标） │
├ 解码列表（可折叠，占下半区）      │
├ 横滑功能带：频段 · 步进 · 滤波 · ATT/NB/NR │
├ 动作行：PTT(两步) · 记 QSO · 开始解码      │
└ 状态行：链路/活性/后端 · 「更多」抽屉       │  ← 面板那 10 个入口进抽屉，不再 exec()
```

配套的两件硬改动：① 模态 `exec()` → modeless 页内视图（8 个对话框的生命周期逐个改，
现在全是栈上 `dlg.exec()`）；② 页签与页表同源（一张 `struct PageSpec{name, build, scrollable}`
表驱动，杜绝 2.1 第 4 条）。

### 2.4 前置条件与验收（这条卡在哪）

- **必须插手机**。本仓纪律是「UI 必看真机一帧」，而现在 `adb devices` 是空的——
  所以第二部分我只做审计和方案，**不动布局代码**。
- 验收清单要加一节 **H 单屏任务完成度**：不切页完成「调频 / 换制式 / 发射 / 记 QSO」四件事，
  每条给 adb 命令与判据（沿用 `docs/android-真机验收清单.md` 的写法）。
- 顺带收益：抽屉化会**减少可滚页数量**，#101 的滚动割裂暴露面自然变小；
  但它不是根因修复，#101 仍单独挂着。

### 2.5 风险

- 模态→modeless 改 8 个对话框的生命周期（谁持有、什么时候析构、返回键谁吃），
  每个都要真机过一遍，不是一晚上能完的事。
- 手机页 1 加交互 = 动 `WaterfallWidget`（GL 控件）。GL 上叠文字/子控件在
  `QOpenGLWidget` 上合成不可靠——桌面那版是**故意把刻度放在父布局里**的，手机照同一招。
- 与 #23 那条 PR 栈的关系已经不大了：安卓链的内容早已进 main（`HANDOVER.md` §6 记过判据）。

---

## 第三部分 要你拍的

1. **MCP 走 C（Python 薄适配器只吃总线）吗？** 第一版 9 个工具够不够？
   先只做 stdio（M0–M2），还是直接排到 Streamable HTTP（M3）？
   rsba1-core 那份 MCP 是合并成一个门面还是并存（M4）？
2. **手机 UI 接受「单屏 + 抽屉」这个目标形态吗？** 给不给插手机的时间窗？
   （不给窗我就只动第 2.1 条里那条「页签与页同源」的表驱动补丁——它不需要真机也能验。）
3. **顺序**：我建议 MCP M0/M1 先走（不碰 UI、能独立验收、直接兑现「agent 可调」这个
   唯一差异点），手机 UI 等 adb 可用再动。

## 附：本文引用的现场

- 本仓：`apps/desktop/bus/bus_server.cpp`、`apps/desktop/session/`、`core/safety.*`、
  `apps/desktop/app/MainWindow_android.cpp:60-230`、`MainWindow_panels.cpp:396-450`、
  `apps/desktop/app/WaterfallWidget.cpp`、`docs/research/wo273-verdicts.md`（C2/C3/E 节）、
  `docs/research/wo273-integration-map.md`（§2.3）。
- 姊妹仓：`rs-ba1-reverse/src/rsba1/mcp/{server.py,__main__.py}`、`docs/MCP_CLIENT.md`、
  `tools/mcp/mcp_smoke_test.py`。
- 官方：`modelcontextprotocol.io/specification/2026-07-28` 与其 changelog 页。
