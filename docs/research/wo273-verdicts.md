# WO-273 撞车裁决与双端路线（C+D+E+F 节）

- 前置阅读：`docs/research/radio-competitors-2026-10-25.md`（13 项总表 + 六层矩阵）
- 本文件只做裁决，不复述证据；阶段二/三须师傅点头后动工

## C · 撞车裁决

### C1. 红海三层（不碰）

| 层 | 被谁占死 | 边际收益 |
|---|---|---|
| 协议栈（RS-BA1/CAT 客户端库） | rigplane（MIT，3.0 beta，11 层包，5600+ 测试，PyPI+文档站）+ kappanhang 教材 | 接近零——Python 世界装 rigplane 即得；我们唯一价值是把它没覆盖的 RS-BA1 V2 音频/0x27 补出来 |
| 通用 SDR UI | sdroxide/AetherSDR（SoapySDR 系全功能） | 零——不做 SDR 采样层 |
| 全模式工作站（浏览器或桌面） | **wfweb 已做完**：FT8/JS8/CW/FreeDV/APRS 全在浏览器里跑，110★ 活跃 | 零——任何"浏览器里再做一遍数字模式"都是给 wfweb 捐时间 |

### C2. 真空三层（逐条验证"为什么没人做"）

1. **Android 端开源 RS-BA1 客户端**：没人做的理由是 RS-BA1 协议逆向门槛高且 IC-705 用户量小。**这个理由对我们不成立**——rsba1-core 已 E2E 验证协议、radio-suite 已有 C++ 字节级互锁实现，门槛我们一年前就迈过去了；剩下的只是壳。窗口期真实存在，但 K40 装机量小 → 先用 E1′ PWA 零成本探需求，PWA 用量数据决定是否下注 E3 原生。
2. **IC-705 + 日志 + agent 的闭环**：全场没有一家把「电台控制 → QSO 落库 → agent 可查询」做成一件事。wfweb 有 ADIF 但无 agent；rigplane 有 rigctld 但无日志、无 MCP；rsba1-core 有 MCP 但无日志无 UI。没人做的理由是"都是发烧友工具，agent 是新变量"——**这个理由正在失效**（我们自己的 scratchpad 就是第一个消费者）。
3. **headless 可被嵌入的总线**：wfweb 有 REST 但绑定自家前端；rigplane 的 web UI 自成体系。真正「radio-as-a-service 给任意宿主（lumo/scratchpad/自动化脚本）」的定位没人做——因为它对纯业余玩家没价值，**对 agent 生态是刚需**。

### C3. 唯一差异点（一句话）

> **别人做电台客户端，我们做「能被 agent 调用（MCP/总线）、能把 QSO 直接落进 HamLog、能嵌进 lumo 的电台后端 + 双端薄壳」——协议栈用 MIT 的 rsba1-core，壳用 AGPL 的 radio-suite，两头许可都干净。**

这句话站得住：三个要素（MCP 工具已 32 个、HamLog 共库已实测、总线已 curl 验收）都有可运行证据，且不存在"rigplane 加两个包就能追平"的快捷路径——它的开源核心计划里没有 agent/HamLog 生态位（rigplane-pro 是闭源商业化方向，动机相反）。

### C4. 最危险的对手与时间窗

rigplane 3.0 还是 beta（pip --pre），但其 web UI（Mobile 皮肤）已经能在手机浏览器用。**我们的手机窗口期 ≈ rigplane 3.0 stable 发布后的 1–2 个版本周期**（约 2–3 个月量级）。结论：PWA 主路必须"借力不竞速"——直接消费本仓总线（`/api/state` `/api/spectrum`），不与它的 Mobile 皮肤拼 UI 完成度，拼 agent/日志闭环这个它没有的维度。

## D · Windows 端裁决（delta only）

**结论：维持 268 的 C++20 + Qt6。**

理由（一句）：桌面端本质是「音频/频谱实时性 + 原生仪表交互」，Qt6 已实测全绿（冷启 2.38s / RSS 135–163MB / 23→49 测试），换栈是纯亏损。

来自竞品的三条修正（进 268 里程碑，不改技术选型）：
1. **多传输后端抽象**（学 sdroxide 的 backend trait）：`ICivTransport` 已是接口（mock/udp/pybridge 三实现），保持"新传输 = 新文件"，禁止在 session 里 switch 传输类型。
2. **Qt6 模块化分层**（学 AetherSDR）：core/dsp/audio/logbook/bus/app 六 target 已隔离；后续任何 Qt 依赖只准进 app/logbook，core/dsp 保持纯标准库（现在就是这样，写成规矩）。
3. **headless 服务化边界**（学 wfweb）：`--headless --bus` 与 GUI 共用同一 command handler（工单硬约束 4，已遵守）；wfweb 的教训是 REST 与前端耦合——我们的 `/api/*` 保持稳定契约，前端（PWA）永远只吃总线。

验收变化：无（268 的 M2 性能红线 500 帧/40ms 不变）。

## E · 手机端裁决

**主路 E1′ = PWA**（`apps/web/`，消费本仓总线；退路 E3 Kotlin 薄壳）。换路量化门槛（师傅默认值照抄）：局域网 PTT→出功率 > 200ms，或 WebAudio 缓冲压不进 100ms，才允许启用退路。

四个硬问题的回答：

1. **离线独立性**：不要。理由：手机永远在电台同一 LAN（IC-705 的 RS-BA1 本来就是 LAN 协议）；PWA 静态资源缓存即可断网可用 UI，音频/控制本来就需要桌面在线——**没有"手机独立跑 RS-BA1"的真需求，只有"在客厅躺椅上用"的假想敌**。
2. **音频延迟**：SSB 可用门槛 ≈ 150–200ms（ITU-T G.114 单向 150ms 内为"可接受对话"）；三方案：E1′ WebAudio + WebSocket 二进制流可压到 80–150ms（wfweb 已实证浏览器链路可对话）；E3 Kotlin AudioTrack 直推可到 40–80ms；E2 Flutter 100–200ms（插件跳一层）。给依据：wfweb 的 CW 侧音"仅 RX 缓冲延迟"证明浏览器路径在 LAN 内可用。
3. **后台存活**：E1′ PWA 前台可用、切后台音频暂停（除非 MediaSession 后台音频 API，Android Chrome 支持；死法 = 省电杀 tab）；E2 Flutter 前台服务可行但音频焦点抢夺要自己写；E3 Kotlin 前台服务 + 电池白名单 + AudioFocusRequest 是标准解，死法最少但只服务一台机器。
4. **连接方式**：LAN/WiFi 为主（RS-BA1 原生）；USB OTG 串口做控制可行但带宽/驱动碎；**蓝牙 = BLE 串口 ≈ 数 kbps，传音频不可能**（SSTV/语音均超两个数量级）——这条写死，任何"蓝牙传音频"的方案直接否。

## F · 仓结构裁决

**单仓 monorepo。** 当前 radio-suite 平移：

```
radio-suite/
├── apps/desktop/    # 现 src/core,dsp,app,logbook,audio,bus 平移（target 语义不变）
├── apps/web/        # 阶段二 PWA（阶段二批准后建）
├── libs/protocol/   # rsba1-core 以 subtree 引入（保留 MIT LICENSE 文件与头）
└── libs/dsp/        # 现 src/dsp + 未来 0x27 解帧/音频 codec（双端共用）
```

- 代价：Qt6 CI 与未来 PWA CI 同仓（用 paths 过滤即可，可控）；换来的"协议/DSP 只写一份"远大于此。
- **rsba1-core 引入方式**：subtree（不是 submodule）——MIT 代码进 AGPL 仓合法且链路简单；反向（AGPL 代码进 MIT 仓）永远禁止，此约束写死在本文件与两仓 README。
- 268 的 CMake target `core/dsp/audio/logbook/bus/app` 搬到 `apps/desktop/` 后语义不变；`bus` 仍是 headless 总线唯一出口。
- 移植窗口：阶段三（师傅点头后）执行平移，平移本身零逻辑改动、纯 `git mv` + CMake 路径修正 + 一次全绿验证。
