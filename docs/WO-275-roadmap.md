# WO-275 · 继续推进总纲（2026-11-17 起）· 自主检测版

> 本文是计划文档，本身走独立分支 `docs/wo275-roadmap` 进 main，**不蹭任何在开的 PR 分支**。
> 执行者：本人（268 领取人）。师傅 2026-11-17 定调：项目继续推进，路线由执行者裁定，**工单内不设询问节点**。
> 本文**取代 `docs/HANDOVER.md` §5 的排序口径**；§5 里的事实条目继续有效，只是不再代表优先级。

---

## §0 执行规矩（自主检测条款，全单适用）

### 0.1 什么叫「自主检测」

1. 每个验收项自带判据：**命令 + 期望输出 + 阈值**。执行者自己跑、自己判、结果原样贴进交付说明或 `evidence/`。
2. 判据跑不出来 ⇒ 该项写 **「未验」** 并写清为什么没跑成（缺工具 / 缺设备 / 缺权限 / 缺网络）。
   **禁止**把「代码看起来对」写成「已验」，**禁止**把 CI 绿当成验收结论——CI 只证明三平台能编能跑用例，
   不证明界面长得对、不证明射频行为对、不证明手机上也绿（安卓专属分支在桌面上根本不参与编译）。
3. 不向师傅提「要不要 / 是否 / 可以吗」。遇到必须由人拍板的**产品形态变更**，本文直接给出裁决与代价，
   写成事实（见 §13 不做清单），不写成问题。
4. 每条 lane 开工前先跑一次「基线自证」：构建 + ctest 全绿 + 记录测试总数。总数变了必须能说出变化的文件与用例名。

### 0.2 证据落盘

- **不可重跑的现场读数**（真机 logcat、时延、fps、截屏、设备回包字节、空口捕获）一律落
  `evidence/<YYYY-MM-DD>-<主题>/`，文件名带机时，目录内放 `NOTES.md` 写清采集命令。
  已有先例：`evidence/2026-11-15-android-n1-n3/`。
- **可重跑的判据**（ctest、grep 计数、构建产物哈希）写在交付说明里，不占 `evidence/`。
- 现场读数量到就记，不追求"再跑一次也一样"——再跑一次就不是同一个现场了。

### 0.3 跨层核对的锚点

- 以物理 / 协议锚点为锚：CI-V 命令字节、UDP 包偏移、端口号、引脚号、UTC 时标，
  **不以变量名 / 函数名 / 文档标题为锚**（同名不同义是这个仓最常见的错）。
- 改「会改外观」的东西用穷举比对（逐格数值、像素 diff），不靠眼看。
- 有单测的纯函数，交付前确认生产代码真的编译进去了（grep 命中数 0 ⇒ 测试是摆设；
  瀑布刻度能印出 `-0.2k` / `24.07k` 就是这么漏出去的，见 `ddc094d`）。

### 0.4 加法纪律（本文**不**获得的权力）

- 真机 `Log.db` / `aprs.db` / 用户 settings 只读优先；写侧只在临时副本或应用私有目录验。
- 交付只用「新分支 + PR」；**PR 合并授权按次生效，一次一句话**，§3 的合并动作执行前在会话里取当次指令，
  不把历史授权外推，也不把本文当成预授权。
- 上游（rigplane 及任何非本仓 issue / PR / 评论）发布**另纸批准**，本文不含。
- 破坏性 git 操作（`reset --hard` / `clean` / 删分支 / 强推）不在本文任何 lane 里。

---

## §1 现状基线（2026-11-17 实测，逐项给出处）

| 项 | 值 | 出处 / 复现命令 |
|---|---|---|
| 远端 main | `b42b3f0`（2026-11-16T09:44:49Z，Merge PR #61） | REST `GET /repos/fe1iscurc0r/radio-suite/commits/main`；本机三平台 CI 已由 #126 确认全绿 |
| 在开 PR | 4 条，全 `mergeable_state=clean`，三平台 success | `GET /pulls?state=open` → 65 / 66 / 67 / 68 |
| 本机测试数 | **266 / 266 通过**（@ `282730c`） | `rs-sat/build/Testing/Temporary/LastTest.log`（2026-11-17 00:43–00:44）`grep -c '^Test Passed.' = 266`，无失败行 |
| ⚠ 同目录坑 | `LastTestsFailed.log` 是 00:43:05 那次（修好**之前**）的残留，ctest 不清它 | 别拿它当结论；要判定看 `LastTest.log` 的逐条 `Test Passed.` |
| main 测试数 | 256（**推算** = 266 − 卫星线 10 条，未单独实跑） | 卫星线用例名见 §5.6；实跑确认排在 L0 |
| 桌面工具链 | MSVC 14.44（VS2022 BuildTools）+ Qt 6.8.3（`D:/Qt`）+ CMake/Ninja | `docs/HANDOVER.md` §6 开头 |
| 安卓工具链 | Qt for Android 6.7.3 `arm64-v8a` + SDK + NDK 26.1 + adb | 同上 §6.1 |
| 真机 | Redmi K40 / Android 13 / arm64-v8a / MIUI V14.0.6，横屏可用区 843×376dp，`wm density` 440（dpr 2.75） | 验收清单 §K4 |
| **硬件缺口** | **没有可连的 Icom 电台**（手上只有全盛 UVK6，不是 IC-705、没有 50003 音频口） | `docs/HANDOVER.md` §5 第 11 条 |
| 卫星地基 | `radio_sgp4`（vendored dnwrnr/libsgp4，Apache-2.0）+ 官方向量逐行对账，门限 1 m | `third_party/CMakeLists.txt:32-61`、`tests/test_sgp4_vectors.cpp` |
| 在开任务 | #97 瀑布即调谐、#100 G-6 断链复核、#101 手机滚动左端割裂、#106 Linux Qt6.10 崩溃、#121/#128 灭屏收包 | 任务台账 |

**基线里最值得注意的一条**：`docs/HANDOVER.md` §5 挂了至少 **17 个「等真机」的复验项**
（50003 四条、APRS 四条、FT8 发射四条、G-1…G-6、WSJT-X 联调五条），它们的阻塞条件是同一句话——
「手上没有可连的电台」。这条阻塞**是假的**：本仓已经有一个逐字节对照过 IC-705 真机抓包的假电台
（`tests/fake_rsba1_server.cpp`，fixtures 的真机基准日期 2026-08-18），它现在只绑 `127.0.0.1` +
随机端口、只被测试目标链接。**把它 LAN 化，这一整堆债今天就地变成自主可验**——这是本单的最大杠杆，排在 L1。

---

## §2 路线裁决与排序理由

裁决原则（按权重从高到低）：**实际性能 / 综合体验 > 硬件有无 > 成本**；不以「开发方便」为主因；
官方参考设计先导，只有官方文档要求或实测不匹配才改装。

| Lane | 内容 | 为什么在这个位置 | 依赖 | 估算 |
|---|---|---|---|---|
| **L0** | 收口：合 4 条绿 PR + main 复验 + 文档落地 | 1 小时量级，是后面所有 lane 的地基；不合，卫星线和保活修复就一直悬在分支上 | 无 | 0.5 天 |
| **L1** | **假电台 LAN 化** → 手机端链路验收自助化 | 一次性拆掉 §5 里最大的一块「等硬件」债；改的是工具不是产品代码，风险低 | 无 | 1.5–2 天 |
| **L2** | 卫星页 ③④⑤（星历源 + 过境预报 UI + 双 VFO 自动跟踪） | 地基（#67/#68）刚打完，热乎；纯软件即可验到底；是本仓对 SDR++/wfview/rigplane 的**唯一功能差异化**方向 | L0 | 3–4 天 |
| **L3** | 幅度域批处理 + 录制/回放 + 解码统计（调研 §8-1 / §8-3） | L4 的量具；同时是长期联调基础设施（「联调不再依赖电磁环境」） | L0 | 2–3 天 |
| **L4** | 手机端渲染缝 #101 **定因**（不是修好） | 没有 L3 的帧录制，这条只能靠猜；4 条修法已被证伪，别再从原路进 | L1, L3 | 2 天（定因）|
| **L5** | 平台债 #106 Linux + Qt 6.10（bus 析构 double-free / SQLite SegFault） | 只在 Linux 腿复现，与产品主线正交，插空做；不做完 CI 永远是「绿但脆」 | 独立 | 1–2 天 |
| **L6** | MCP / PWA 的下一步（消费卫星数据、手机浏览器端） | 有价值但不阻塞任何 lane，本单只留接口不排工 | L2 | 另单 |

**为什么不是「先去啃 #101」**：#101 是渲染正确性问题，快且脏，但它的四条直觉修法已经全部实测证伪
（见 §7.1），第五条路（读 Qt 安卓源码）本机没有源码，第六条路（换 Qt Quick）是产品形态变更——
本单**不批**。在缺量具的情况下继续投入就是烧时间。

**为什么 L1 排在功能开发前面**：L2 做完要上真机看，而真机验收的门禁链路（G-1…G-6）本身是坏的
（只能连 mock）。先修门，再往里搬东西。

---

## §3 L0 · 收口四条在开 PR

### 3.1 待合清单（2026-11-17 实测）

| PR | 分支 | base | head | 规模 | CI |
|---|---|---|---|---|---|
| #67 | `feat/satellite-sgp4` | main | `d0a2ec8` | +7234/−1 · 47 文件 | U/W/M success |
| #68 | `feat/satellite-doppler` | **feat/satellite-sgp4** | `282730c` | +717 · 9 文件 | U/W/M success |
| #65 | `docs/main-sha-and-research-corrections` | main | `46bc128` | +128/−28 · 5 文件 | U/W/M success |
| #66 | `fix/listen-keep-alive` | main | `4ff25b2` | +770/−1 · 18 文件 | U/W/M success |

### 3.2 顺序与为什么

`#67 → #68 → #65 / #66`。**#68 的 base 是 #67 的分支**，先合 #68 会把它自己的 base 变成已合并分支，
GitHub 会把 diff 重写成「相对 main」——那时 #68 的净增内容要重新核一遍，白做工。
#65/#66 直接对 main，彼此独立，可并行。

### 3.3 施工步骤

- **W0.1** 合并前逐条取：head sha、三条 check-run 的 `conclusion`、`mergeable_state`。任一不是
  `success` / `clean` ⇒ 停下，不合，写进交付说明。
- **W0.2** 回滚点记档：`b42b3f0` 写进 PR 评论与本文 §3.6，合完每条再补一行「合前 main = …」。
- **W0.3** 合并方式与既有惯例一致（merge commit，不 squash——历史里 `Merge pull request #NN` 是主形态）。
- **W0.4** 合完跑 §3.4 的验收门。
- **W0.5** 远端 `git` 协议被代理重置时（`Recv failure: Connection was reset`）走 REST + 系统代理，
  不要把重试当成「凭据坏了」；`api.github.com` 通而 `github.com/git` 不通是本机常态。

### 3.4 验收门（自主检测）

| 编号 | 判据 | 命令 | 期望 |
|---|---|---|---|
| A-0.1 | main 前进且四条都进来了 | REST `compare/b42b3f0...main` | `ahead_by ≥ 4`，含 4 个 merge commit |
| A-0.2 | main 三平台 CI 全绿 | `GET /commits/<main>/check-runs` | 3 条 `completed/success`，无 queued |
| A-0.3 | 本机全量重跑 | `cmake --build build && ctest --test-dir build` | `100% tests passed, 0 failed`，**总数 = 266** |
| A-0.4 | 差集核对（**不能用 tree 相等**） | 每 PR `GET /pulls/N/files?per_page=100` 逐个 blob sha 比对 | 见 §3.5 的坑 |
| A-0.5 | 卫星线用例名清单落地 | 本文 §5.6 | 10 条，与 A-0.3 的 266 − 256 对得上（顺手把「推算」变成实测） |

### 3.5 本 lane 已知的坑（照抄，别重踩）

- **连着合多条时不能核 `merge^{tree} == head^{tree}`**：后一条的 tree 里含前一条的内容，等式必然不成立。
  要按「每条 PR 相对**它自己的 base** 的差集」逐文件核 blob sha。
- 合并完本地 `main` 是旧的：本机 `main` ref 停在 `0784a15`，`git ls-remote` 又被代理拦，
  取远端事实一律用 REST，不要信本地 ref。
- 主工作树 `D:/my git/radio-suite` 目前 checkout 在**已作废**的 `ui/quick-interactions` 上，
  还带着未提交的 kanban WIP（7 项）。L0 之后所有施工**一律开新 worktree**，不在主工作树里动。

### 3.6 记档位

```
回滚点（合并前 main）: b42b3f0
#67 合入后 main = ____   时间 ____
#68 合入后 main = ____   时间 ____
#65 合入后 main = ____   时间 ____
#66 合入后 main = ____   时间 ____
```

---

## §4 L1 · 假电台 LAN 化：把「等真机」变成「自己验」

### 4.1 事实基础（已核）

- `tests/fake_rsba1_server.h` 头注释写明模拟三信道：`50001 ctrl`（pkt3/4/6、login、auth 0x02/0x05、
  0xA8 下推、ConnectTrans、deauth）、`50002 serial`（CI-V 双向透传）、`50003 audio`（20 ms 帧、24 B 头，
  kappanhang `sendPart1/2` 布局）。
- 应答与 `tests/fixtures/*.hex` **逐字节一致**，fixtures 由 rsba1-core 参考实现生成并经 **IC-705 真机验证 2026-08-18**。
- 现状限制：`tests/fake_rsba1_server.cpp:59-61` 三张 socket 全绑 `QHostAddress::LocalHost` 且端口传 `0`
  （系统随机），且只在 `tests/CMakeLists.txt:56` 被 `radio_tests` 链接 → **产品外的进程用不了**。
- 同类先例：`tools/civ_tcp_gateway.cpp` 已经是「台架小工具」形态，`--bind` 的帮助文本原话是
  「局域网给手机连时填 0.0.0.0」，`--mock` 直接喂 `MockTransport`。手机端 **CI-V over TCP** 这条链路
  今天就能零改码验，缺的是 RS-BA1 UDP 三信道那条。
- 安全先例：`apps/desktop/bus/bus_server.cpp:129` 已有「非 loopback 绑定且无 token 就拒绝」的闸。

### 4.2 施工

- **W1.1 目标 `tools/fake_rig`**：独立可执行，链接 `fake_rsba1_server.cpp`（**不进产品二进制、不进 APK**，
  CMake 里挂在 `RADIO_SUITE_BUILD_BENCH_TOOLS` 开关下，默认 OFF，与 `BUILD_ENGINE_SPIKE` 同构）。
- **W1.2 参数化**：`--bind <addr>`（默认 `127.0.0.1`）、`--base-port`（默认按 50001/50002/50003 固定口，
  不再让系统挑——手机侧要能填得进去）、`--scenario <name>`、`--fault <name>`。
- **W1.3 绑定闸（照抄 bus 的先例）**：非 loopback 绑定必须显式 `--i-know-this-is-lan`，且启动横幅打印
  一行红字警告「本进程假扮电台，只在受控局域网跑，不做任何鉴权」。不满足就直接退出，不静默开放。
- **W1.4 故障场景**（每个是一条可复现脚本，不是一次性手点）：

| scenario | 验的是 | 对应欠账 |
|---|---|---|
| `nominal` | 握手 → 认证 → 建流 → 三信道全通 | G-1 / G-2 |
| `silence-after-N` | 回帧在 N s 后彻底停发 → 断链指示是否**持续** | **G-6（#100：断链持续指示缺失 + 断链后白屏定性）** |
| `slow-handshake` | 握手拖 5 s，UI 不许提前显示在线 | K3 诚实状态栏 |
| `nak` | CI-V 回 NAK，会话层要如实上报（A2 语义） | A2 复核 |
| `auth-deny` | 0x02/0x05 拒绝，错误文案与降级路径 | G-3 |
| `audio-rx-only` | 只喂 50003 下行，验真实 PCM 频谱不是合成谱 | HANDOVER §5 第 5 条 ①④ |
| `audio-loopback` | 50003 进 + PTT 门禁麦克风上行（半双工静音） | 50003 四条 |
| `flap` | 建链后拔网/重绑，恢复行为与不谎报 | G-4 / G-5 |

- **W1.5 手机端跑法**：PC 起 `fake_rig --bind 0.0.0.0 --i-know-this-is-lan`，K40 用应用内「连接…」
  对话框（G-0 那条已验通的路径）填 PC 局域网 IP → force-stop 重启 → 逐 scenario 收 logcat + 截屏，
  落 `evidence/<date>-fake-rig-lan/`。**Windows 防火墙要放行 50001-50003/UDP**，这是本 lane 第一个会撞的墙。

### 4.3 口径红线（这一条决定本 lane 是加分还是埋雷）

> 假电台验的是**「本仓客户端与本仓已归档的 IC-705 字节级 fixtures 是否一致」**，
> 它**不验**真机时钟漂移、真实 50003 抖动与丢包、射频 PTT 尾、实际延迟与音质保真。

因此：

- 经假电台打勾的每一项，在 `docs/android-真机验收清单.md` 与 `docs/HANDOVER.md` 里必须写成
  **「LAN 假电台验通」**，与「真机验通」是**两个不同的勾**，不许合并、不许省略限定词。
- 验收清单表头加一列口径标记（真机 / LAN 假 / mock / 静态核对），把历史条目也一并标上——
  历史里「代码事实」那部分本来就只是 PC 静态核对（源码行号、`aapt` 出包信息、jar 字节码）。
- 这个限定词同样写进 `tools/fake_rig` 的文件头注释，防止后来人只看代码就以为它等价于真机。

### 4.4 验收门

| 编号 | 判据 | 命令 / 现场 | 期望 |
|---|---|---|---|
| A-1.1 | 工具默认不外放 | `fake_rig` 不带 `--bind` 启动后 `netstat -ano \| findstr 5000` | 只见 `127.0.0.1` |
| A-1.2 | 非环回需要显式旗标 | `fake_rig --bind 0.0.0.0` | 直接退出并打印原因，不监听 |
| A-1.3 | 三信道固定端口 | `--bind 0.0.0.0 --i-know-this-is-lan` | 50001/50002/50003 三个 UDP 在听 |
| A-1.4 | PC↔PC `nominal` 通 | 桌面 app 填 `127.0.0.1` | 状态栏显示 udp 链路 + 频率随假电台变，S 表非 -999 |
| A-1.5 | 手机↔PC `nominal` 通 | K40 logcat + 截屏 | 同 A-1.4，且落 `evidence/` |
| A-1.6 | `silence-after-N` 判**断链持续指示** | 断流后 ≥ 3× 活性窗口的连续采样 | 断链指示**持续存在**，不回绿；断链后不白屏（#100 两问各有答案） |
| A-1.7 | 8 个 scenario 全跑过 | 一张表：scenario / 期望 / 实测 / 勾的口径 | 无「未跑」 |
| A-1.8 | 产品没被污染 | `unzip -l` APK + `grep -c fake_rig` | APK 内无 fake_rig；产品 target 不链 `fake_rsba1_server.cpp` |

---

## §5 L2 · 卫星页 ③④⑤

### 5.1 地基（已进 main 之前，本文按 L0 合并后写）

`apps/desktop/satellite/sat_track.h` 现在只出数、不碰 Qt、不碰会话，对外四个能力：

```
look_at(tle, tsince_min, Station) -> {az_deg, el_deg, range_km, range_rate_kms}
doppler_hz(rf_hz, range_rate_kms)                  -> 相对标称的偏移量
linear_uplink_hz(f_up_hz, range_rate_kms)          -> (1 - ṙ/c)^-2（线性星「补两倍」）
find_passes(tle, st, from_min, to_min, el_floor)   -> [{aos, tca, los, max_el, min_range, max|ṙ|}]
```

单位坑已在这一层收敛完（`Observer` 入参：度 / 度 / **公里**；`GetLookAngle` 出参：方位仰角是**弧度**、
斜距公里、ṙ 公里/秒；`tsince` 是相对 TLE 历元的**分钟**）——新代码不许再造第二套时间轴。

### 5.2 W2.1 时基（先做，最小，且是后面一切的输入）

- `tsince = (UTC_now - TLE_epoch) / 60`，含跨日 / 跨年 / 闰秒无所谓（SGP4 用 UT1≈UTC，本仓不引 EOP 自算）。
- 用例至少覆盖：历元前（负 tsince）、跨年、D-2、TLE 历元精度只有 0.1 分钟时的量化影响（写清误差量级，别当 bug 修）。

### 5.3 W2.2 星历来源（过境预报没星历等于没数据）

- **数据面**：Celestrak 分组（`https://celestrak.org/NORAD/elements/gp.php?GROUP=<name>&FORMAT=tle`），
  拉取 → 逐条过 `tle_check`（mod-10 校验和 + 结构，PR #68 已入库）→ 坏行进 `quarantine/` 并报数，
  **不许静默丢弃、不许自动修**（补齐 69 列不是修复，见 `tle check: padding a short line to 69 columns is not a repair`）。
- **网络纪律**：默认**不联网**。CI 与离线自测吃仓库内的 fixture 星历；真拉取要显式开关。
  带 `User-Agent`、带最小刷新间隔（同一分组 < 2 h 不吃网络）、ETag/If-Modified-Since 能省则省。
- **缓存**：落 `settingsDir()`（D1 那条已验通的可写落点），带来源时间戳；UI 必须显示「星历 X 分钟前」。
  星历过期时过境表**变灰不许直接用**——拿三天前的 TLE 算 AOS 报错半小时是运营事故不是精度问题。
- **许可**：TLE 数据本身不受版权保护，但 Celestrak 有使用条款；本仓只缓存不分发，NOTICE 里加一条出处。

### 5.4 W2.3 卫星页 UI

- 形态裁决：进安卓分页版做**第 6 个页签「卫星」**（现有 5 页：频谱 / 调谐 / 制式 / 面板 / 日志），
  桌面在主窗右侧面板区挂同一份 widget。**不做竖屏**（已拍板放弃）、**不换 Qt Quick**（§13）。
- 一屏只回答四件事：①选星 ②此刻 az/el/斜距/ṙ ③下一次过境（AOS/TCA/LOS/最大仰角）④上下行该打多少。
- **纵向预算先算**（本仓 UI 铁律：仪表/旋钮压缩同排、新控件先算纵向预算）：横屏可用高 376dp，
  顶栏 + 页签栏先扣掉，卫星页四块内容各给多少 dp 要在提交前写进 PR 正文，超预算就砍块不砍字号
  （按钮最小 44dp 不许破）。
- 过境表点击 → 把 RX 填到下行标称、TX 填 `linear_uplink_hz` 的结果，**只填不发射**（发射仍走 SafetyGate 与两击门禁）。

### 5.5 W2.4 双 VFO 自动跟踪（④，最后做）

- 复用会话层已有的 `0x25/0x26` 读 A/B、`0x07` 选 VFO、`0xB0` 交换与 `effectiveTxHz`；
  这一层只出「该改成多少 Hz」的策略，不直发命令。
- **写入节流是硬要求**：多普勒按帧算会每帧给一个新频率，直接写电台等于把 CI-V 通道打爆。
  策略：`|Δf| < 死区` 不写；两次写之间 ≥ 最小间隔；且**发射中不改频**（既有约束：发射中拒绝切换 VFO）。
- 过境边界：AOS 前 N 秒预置、LOS 后自动退出跟踪并**显式停**（跟踪模式在屏上要有指示灯，不许「悄悄还在跟」）。
- 目标漂移可视化：实际跟踪的频点 vs 计算频点，两条线同屏，偏差写进状态行。

### 5.6 现有卫星线 10 条用例（基线自证要用）

```
sgp4: vendored libsgp4 reproduces the official reference output
satellite: doppler sign, magnitude and the doubled uplink correction
satellite: pass geometry closes against Kepler's third law
satellite: range rate crosses zero at closest approach
satellite: pass windows bracket the elevation floor
tle check: official vector corpus validates, probes do not
tle check: a single flipped digit is caught and the delta is reported
tle check: paste noise is normalized, not silently repaired
tle check: structural problems are each reported independently
tle check: padding a short line to 69 columns is not a repair
```

### 5.7 验收门

| 编号 | 判据 | 期望 |
|---|---|---|
| A-2.1 | 过境预报**外部对账**（自算自比不算验） | 拿 ISS / SO-50 各一次真过境，与公开预报比对 AOS/TCA/LOS，误差 ≤ 1 min、最大仰角 ≤ 1°，比对来源与截图进 `evidence/` |
| A-2.2 | 变异对照（证明用例能抓错） | 故意把站是经度填成纬度 / 把 ṙ 符号取反 / 把转发比当成多普勒系数，三处各必须让对应用例 FAIL；改回全绿 |
| A-2.3 | 星历坏行不静默 | 注入 3 条校验和错的 TLE，`quarantine` 计数 = 3 且 UI 有「N 条星历被拒」 |
| A-2.4 | 离线可跑 | 断网 + `ctest` 全绿；CI 里无外网请求（grep 构建日志无 celestrak） |
| A-2.5 | 星历过期降级 | 把缓存时间戳改到 3 天前 → 过境表变灰、不许一键填频 |
| A-2.6 | 写频节流 | 120 s 过境跟踪内实际下发的写频命令条数有上限（日志计数），且发射中 = 0 条 |
| A-2.7 | 手机真机一帧 | K40 横屏卫星页截屏进 `evidence/`，按钮实测最小高 ≥ 44dp，无左侧标签被切（#101 的已知表现） |
| A-2.8 | 门禁不被绕 | 过境填频后直接按发射，非业余段/未两击仍被拒且面板显示「发射被拒绝」 |

---

## §6 L3 · 幅度域批处理 + 录制/回放/统计（调研 §8-1 / §8-3）

出处：`docs/research/open-source-sdr-survey-2026-11-07.md` §8 建议 1、3（§8-2 的 AX.25 FCS 位纠错已进 main，
落点 `a2ddad0`「快捷交互 + 瀑布缩放 + AX.25 FCS 纠错（重挂当前 main）」，计数走 `Afsk::corrected()`）。

### 6.1 W3.1 幅度域共享缓冲（readsb / dump1090 范式）

- 目标：同一份 `I/Q → 幅度缓冲`成为**瀑布、APRS AFSK、SSTV 三者的共同输入**，解调按块扫整数幅度。
- 改动面：`libs/dsp`（`waterfall_frame.cpp`）+ `aprs/afsk.cpp`。
- **性能必须有数字**：改前/改后各测「每帧 memcpy 次数」「每帧幅度计算耗时」「稳态 RSS」。
  指标矛盾时先怀疑量具（本仓吃过两次），量具要先自证（同一份输入跑三遍方差 < 阈值）。
- 不许为了「共享」把三者绑进同一个生命周期：共享的是**只读缓冲**，各家自己取窗口，
  谁的滤波需求不同谁自己在后面做——不做统一解调器。

### 6.2 W3.2 录制与回放（`ifile` 等价物 + rtl_433 `-M` 统计）

- 录制：48k PCM（和可选 I/Q）落文件，带头部（采样率 / 时标 / 来源链路 / 电台标称频率）。
- 回放：同一套解码链吃文件，**不依赖电磁环境**——这条直接决定后面所有联调的速度。
- 计数：`decode_ok` / `decode_fails` / `dropped` / `rejected_by_gate` 按解码器分列，走总线 `/api/state`
  与设备台账页（B2 那张 `QVector<ReportRow>` 的台账，`apps/desktop/app/device_report.cpp`）。
- iGate 阈值自调**本单不做**：先把计数做出来，自调是下一单的事（没有可靠计数的自调是玄学）。

### 6.3 验收门

| 编号 | 判据 | 期望 |
|---|---|---|
| A-3.1 | 共享缓冲真的被三家吃到 | grep 三个消费者都取同一 buffer 句柄；单测断言「同一帧只算一次幅度」 |
| A-3.2 | 性能数字 | 改后每帧幅度计算耗时下降或持平且 memcpy 次数减少，数字进 PR 正文 |
| A-3.3 | 回放幂等 | 同一份录制文件连跑 3 次，FT8/APRS 解码结果逐条一致 |
| A-3.4 | 录制的 fixture 进仓 | 体积 ≤ 单文件上限（走 `api contents` 单文件传，`raw.githubusercontent` 会 32 KiB 截断），CI 里离线可跑 |
| A-3.5 | 变异对照 | 把录制头部采样率改错 ⇒ 解码必须大面积 FAIL，不许静默出「看着像」的结果 |

---

## §7 L4 · 手机端渲染缝（#101）**定因**

### 7.1 已证伪的四条（列出来是为了让人别重试）

1. `QT_NO_FAST_MOVE` / `QT_NO_FAST_SCROLL`——设备上 RSDBG 确认开关**生效**，滞后带一条没少；
2. 父控件 / `window()->update()` 逐拍强绘——桌面侧实测 `paints=0`；
3. 换 Qt 6.8.3 套件——无效；
4. 把该页改成 GL 控件——无效。

已实测：甩动期绘制节奏 **15.9 fps（62–63 ms/帧）**；`gfxinfo` / `SurfaceFlinger --latency`
对 Qt 的 `ANativeWindow` 在这台机上取不到有效数。剩余嫌疑：**安卓侧 backing store → ANativeWindow
的部分区域 flush**，缝在屏幕 **x≈148**。

### 7.2 本单的三条自主取证（先定因，再谈修）

- **W4.1 缝坐标是不是固定的**：改窗口宽度 / 改内容宽度 / 换横竖朝向，看缝是跟 **dp×dpr** 走
  （440 density ⇒ dpr 2.75，148 dp ⇒ ≈407 px）还是恒在物理 x≈148。
  这是「部分区域 flush」与「固定矩形脏区」的分水岭，一次实验就能判，**优先级最高**。
- **W4.2 脏矩形打点**：用 L3 的统计通路加「每帧脏区面积 / 帧号 / 耗时」计数，安卓侧走 logcat。
  没有这个打点，§7.1 的教训会一直重演。
- **W4.3 强制整窗 dirty 的判别实验**：Android 上滚动期把整窗标脏。
  缝消失 ⇒ 定性为部分 flush，修法见 §7.3；缝不消失 ⇒ **推翻现有假设**，本 lane 当场停手并把否定结论写进
  HANDOVER §7（否定结论也是交付，省得下一个人在同一条死路上再花两天）。

### 7.3 判完之后的两条出路（本单只批第一条）

- **允许**：仅安卓、仅滚动期开整窗 dirty（正确性换功耗），且功耗变化要实测记数（不掉帧、不明显发热为过）。
- **不批**：自定义 flush 策略钩子（需要 Qt 源，本机只有二进制，`D:/Qt` 下没有 sources）；
  手机 UI 换 Qt Quick（产品形态变更，见 §13）。

---

## §8 L5 · 平台债 #106（Linux + Qt 6.10）

- 现场：bus 析构 double-free、SQLite 用例 SegFault，只在 Linux + Qt 6.10 腿复现。
- 复现路径：本机 WSL（有 gcc，**无 Qt** ⇒ 用 apt 装或直接在 CI 上跑一个临时 debug job；
  往 CI 塞 job 属改共享流水线，**改动前先取当次授权**，本单不预支）。
- 工具：`-fsanitize=address,undefined` + `--report_bugs=false`，先看是不是 owner/parent 双删；
  SQLite 那条同时怀疑 `QSqlDatabase` 连接名重复注册（Qt 6.9+ 对 remove/open 的断言变严）。
- 交付门：ASan 报告进 `evidence/`；修完 Linux 腿 ctest 与 MSVC 腿**各绿一次**（平台专属代码各平台各构建一次）；
  回归用例必须能抓回退（先把修复关掉复现 crash，再开回去）。

### 8.1 顺带结掉的两条旧任务

- **#97「瀑布图即调谐 + 单屏操作台」→ 本单裁决关闭**：前半条已经落地并在 main 上
  （`a2ddad0`：点击调频 / 刻度 / 滚轮中心缩放 / 框选放大 / 双击复位 / 键盘调谐 / 峰保持线，
  加 #63 的数字键盘输频）；后半条「单屏操作台」**不做**——分页版已经把手机纵向空间问题解决到可用品质
  （五页签 + 顶栏收状态行 + 44dp），再推倒成单屏等于重做一遍 §5.4 的预算，收益不明。
  卫星页按**第 6 页签**加，不按单屏加。
- **#121 / #128「灭屏收包」→ 归 L1**：这两条缺的不是代码而是**可复现的同网段现场**。
  L1 的 `flap` / `silence-after-N` 场景 + PC 侧 UDP 定频打点，正好把它变成一条能反复跑的脚本；
  判定前先用 `ping` 确认手机在 IP 层活着（手机休眠即不收包是量具坑，不是被测行为）。

---

## §9 排期与提交规矩

- 分支：`feat/wo275-<lane>`（例 `feat/wo275-fake-rig-lan`、`feat/wo275-sat-tle`），一律从 **合并后的新 main**
  开 worktree，不在主工作树（脏 + 作废分支）施工。
- 一次提交一件事；PR 正文固定四段：**做了什么 / 判据实跑输出 / 未验项与原因 / 回滚点**。
- 文档与代码**同批**：动了用户能看见的行为就同批改 `docs/HANDOVER.md` §5/§6 与
  `docs/android-真机验收清单.md`；补记不蹭在开的 PR 分支。
- 不混许可：vendored 源码只进 `third_party/`，许可证原文留在子目录并在 `NOTICE.md` 列表。
- **PR 合并**：每条 PR 自己一次授权（§0.4），不因为本文写了顺序就当作已批。
- 每条 lane 收尾跑一次「全量 + 变异抽查」，只跑全量不算过。

节奏（工作日粒度，非承诺）：

```
D1        L0 收口 + A-0.* 全跑
D2 – D3   L1 fake_rig + 8 scenario（PC 先，手机后）
D4 – D7   L2 卫星：W2.1 → W2.2 → W2.3 → W2.4，每个子项独立 PR
D8 – D10  L3 批处理 + 录制回放
D11 – D12 L4 定因三条实验（可提前到 D8 插空）
D13       L5 平台债 / 机动缓冲
```

---

## §10 安全与合规边界（不可协商）

1. **发射**：任何 lane 不得绕过 `SafetyGate`；PTT 两击门禁的回归用例必须**保持能抓**——
   交付前故意把门禁短路一次，确认用例红。
2. **频段**：非业余段不键控的既有约束保持；卫星页「填频」不等于「允许在该频发射」。
3. **假电台**：只跑受控局域网，默认 `127.0.0.1`，非环回要显式旗标且打印警告（§4.2 W1.3）。
   产品代码不得依赖它。
4. **总线**：非 loopback 暴露必须带 token（沿用 `bus_server.cpp:129` 的闸），LAN 手机连总线时也要带 token，
   不许为了手机方便把闸拆掉。
5. **凭据**：token 不落仓、不进 PR 正文、不进 `evidence/`；`git credential fill` 每次重取，不缓存明文。
6. **真机数据**：`Log.db` / `aprs.db` 读侧优先，写侧只写设备私有目录或临时副本；动真库前先备份。
7. **上游发布**：rigplane / 任何外部 issue、PR、评论**另纸批准**，且先复核、中文正文过目、披露 AI 辅助。
8. **不做的 git**：`reset --hard`、`clean -f`、`branch -D`、强推、`--no-verify` 一律不在本单出现。

---

## §11 Definition of Done（一表通）

| Lane | DoD 条款 | 状态 |
|---|---|---|
| L0 | 4 条 PR 合入、main 三平台绿、本机 266/266、回滚点记档 | 未开 |
| L1 | 8 scenario 全跑、手机 PC 各一遍、口径列进验收清单、APK 无 fake_rig | 未开 |
| L2 | 过境预报外部对账 + 3 处变异对照 + 星历过期降级 + 手机真机一帧 | 未开 |
| L3 | 三家吃同一缓冲 + 性能数字 + 回放幂等 + 采样率变异对照 | 未开 |
| L4 | 三条实验各有**结论**（含否定结论）并落 HANDOVER §7 | 未开 |
| L5 | Linux ASan 报告 + 两平台各绿 + 回退能抓 | 未开 |

通用三条，任何 lane 缺一条就不算完成：

- **绿灯≠验过**：平台专属代码各平台各构建一次；UI 必看真机一帧；未验显式标注。
- **判据可复跑**：交付说明里贴的命令别人复制粘贴就能跑出同样的数。
- **文档同批**：HANDOVER 与验收清单不改口的 PR 不收。

---

## §12 风险登记

| 风险 | 触发条件 | 影响 | 缓解 | 回滚 |
|---|---|---|---|---|
| **假电台变成自欺** | 勾不写口径 | 整仓验收可信度报废（比不验更糟） | §4.3 红线 + A-1.7 表头四档勾 | 撤勾并出勘误条 |
| MIUI 锁屏 / 省电干预 | 夜里跑真机 | 截屏回 0 字节、收包停在 IP 层以下 | 白天 + 插电 + 常亮；判定前先 `ping` 证活性（手机休眠即不收包是**量具坑**不是被测行为） | 改测次日 |
| Windows 防火墙吃 UDP 50001-3 | L1 第一次手机连 | 「假电台不通」的假结论 | §4.2 W1.5 里先放行再判 | 删规则 |
| 写频打爆 CI-V | L2 跟踪无节流 | 电台卡顿 / 命令积压 | A-2.6 条数上限 + 死区 | 关跟踪只留读数 |
| 星历源不可用 / 无网 | CI 或野外 | 卫星页变空 | fixture 默认、网络开关化 | 走离线包 |
| 代理吃 git 协议 | 全程 | 交付卡住 | REST + 系统代理；先分诊六层再动凭据 | 不改凭据配置 |
| 大 vendored/fixture 撑爆仓 | 50003 录制文件 | 克隆变慢、raw 截断 | 单文件上限 + 压缩 + `api contents` 传 | 移出为外部样本 |
| 卫星页吃掉手机纵向预算 | 第 6 页签 | 复现 #101 那类左侧标签被切 | 预算先算进 PR 正文；A-2.7 真机一帧 | 砍块不砍字号 |
| 主工作树 WIP 被覆盖 | 在 `ui/quick-interactions` 上施工 | 丢 kanban 未提交改动 | 一律新 worktree；动前 `git status` | 不碰主树 |
| 多 PR 连合后核错等价式 | L0 | 误判「内容丢了」再回滚好事 | §3.5 按各自 base 差集核 | 只停手不回滚 |

---

## §13 明确不做（防止 scope creep）

| 不做 | 理由 |
|---|---|
| 竖屏适配 | 已拍板放弃（「固定横屏效果会好很多」），只保横屏 |
| 手机 UI 换 Qt Quick | 产品形态变更，要人拍板；本单只列代价不启动 |
| FreeDV / RADE 数字语音 | 调研 §8-7 远期，audio 通道形态够了但没有硬件落点 |
| Dire Wolf / liquid-dsp 整仓引入 | 调研 §3 结论：只在灵敏度真不足时再引，现在没有这个证据 |
| iGate 阈值自调 | 先有可信计数再谈自调（§6.2） |
| 自动天线旋转 / 后端方位控制 | 无硬件；且属"改装"，违反官方先导原则 |
| rigplane sidecar 切 3.0 正式版（§8-6） | 上游 gate 阻塞未解除（#4352 未修），不等它 |
| 存储权限 / 「所有文件访问」 | targetSdk 34 不给授，SAF 导出已经能用（验收清单 §2b） |
| 上游 issue / PR 的自动化发布 | 另纸批准，且形态只征求意见、不附 PR |
| Kotlin 原生薄壳（退路 E3） | 主路 E1′ 还没撞墙门槛（PTT 端到端 > 200 ms），提前做是浪费 |

---

## §14 命令速查（复现本单引用的每个事实）

```bash
# 远端事实（git 协议被代理拦时用 REST；token 每次重取，不落盘）
T=$(printf "protocol=https\nhost=github.com\n\n" | git credential fill 2>/dev/null | awk -F= '/^password=/{print $2}')
curl -s -H "Authorization: Bearer $T" \
  https://api.github.com/repos/fe1iscurc0r/radio-suite/commits/main | head -5
curl -s -H "Authorization: Bearer $T" \
  "https://api.github.com/repos/fe1iscurc0r/radio-suite/commits/<sha>/check-runs?per_page=100" \
  | grep -E '"name"|"conclusion"'
curl -s -H "Authorization: Bearer $T" \
  "https://api.github.com/repos/fe1iscurc0r/radio-suite/pulls?state=open&per_page=100" | grep '"number"'

# 本机全量（MSVC 输出是 GBK，管道过 iconv，否则中文全乱）
cmake --build build && ctest --test-dir build 2>&1 | iconv -f GBK -t UTF-8 -c | tail -20
grep -c '^Test Passed.' build/Testing/Temporary/LastTest.log   # 266（别信 LastTestsFailed.log 的残留）

# 假电台 LAN（L1 完工后）
./build/tools/fake_rig --bind 0.0.0.0 --i-know-this-is-lan --scenario nominal   # 防火墙先放行
./build/tools/civ_tcp_gateway --mock --bind 0.0.0.0 -p 4533                     # 手机 CI-V-over-TCP 今天就可用

# 手机侧
adb devices; adb shell wm size; adb shell wm density
adb logcat -v time | grep -iE "radio-suite|multicast|wake_lock"
adb shell am start -n org.radiosuite.app/org.qtproject.qt.android.bindings.QtActivity
adb exec-out screencap -p > evidence/$(date +%F)-sat/phone.png   # MIUI 锁屏时回 0 字节，先解锁

# 卫星线
ctest --test-dir build -R "sgp4|satellite|tle check" --output-on-failure   # 10 条，见 §5.6
```

---

## §15 术语与单位（本仓反复踩过的，钉死一次）

- `tsince`：**相对 TLE 历元的分钟**，不是「相对此刻」，也不是 epoch 秒。
- `libsgp4::Observer` 入参：纬度 / 经度 = **度**，海拔 = **公里**（不是米）。
- `GetLookAngle` 出参：方位 / 仰角 = **弧度**，斜距 = 公里，`range_rate` = **公里/秒**（正 = 远离 ⇒ 收到的频率降低）。
- 线性转发器上行补偿是 **(1 − ṙ/c)^-2**（上下行各吃一遍），不是 `^-1`；转发比只决定标称频率对，不进多普勒量。
- TLE 第 2 行后面可能挂测试参数列，解析只吃前 **69** 字符。
- Gerber/坐标类外部文件先确认单位（`gerbparse` 已返回英寸），EDA 坐标 4.5 版要 ÷1e5。
- `adb shell cat` 会插 `\r`：逐字节比 md5 前先还原，否则假报「不一致」。

---

## §16 本文的失效条件

- L0 合完、§3.6 记档填满之后，§3 整节作废（不再复述），其余 lane 继续有效。
- 出现下列任一情形，**先改本文再动手**，不许绕：新拿到可连的 IC-705（L1 立刻降级为辅助手段，真机优先）；
  上游 rigplane 解除 #4352（WO-274 的 C/D 复活，插到 L2 之前）；产品形态要变（竖屏 / Qt Quick / Kotlin）——
  那三样都不在自主裁量范围内。
- 本文与 `docs/HANDOVER.md` 冲突时，以 HANDOVER 的**事实**为准、以本文为**顺序**准；
  发现事实冲突，先按 HANDOVER 改本文，改不动就停下来写勘误。
