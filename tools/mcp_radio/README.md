# mcp_radio — radio-suite 的 MCP 门面（M2：三读 + 五写）

SPDX-License-Identifier: AGPL-3.0-or-later

把**本仓 HTTP 总线**转成一个 MCP server，让 agent（lumo / scratchpad / 任意 MCP 宿主）
能读电台状态、也能设频/设制式/发射/记 QSO。设计口径来自
`docs/research/mcp-and-phone-ui-2026-11-05.md` 的方案 C：适配器只做转写，**不直连电台**。
直连电台等于绕过 `RadioSession` 的唯一出口、SafetyGate 的 PTT 两击门禁、非业余段拒绝
与 10 分钟自动回 RX —— 那正是我们唯一差异点的反面。

## 怎么跑

```sh
# 1) 先把总线起来（mock 电台就够，不需要硬件）
radio-suite --headless --mock --bus 8765

# 2) 宿主侧把适配器当子进程拉起（stdio 传输）
PYTHONPATH=tools python -m mcp_radio --url http://127.0.0.1:8765
```

MCP 宿主（Claude Desktop / Qoder / lumo）里配的是同一条命令：`command` 指 python，
`args = ["-m", "mcp_radio", "--url", "http://127.0.0.1:8765"]`，并把 `PYTHONPATH` 指到本仓 `tools/`。

| 环境变量 | 默认 | 说明 |
|---|---|---|
| `RADIO_BUS_URL` | `http://127.0.0.1:8765` | 总线地址；给了 `--url` 就以它为准 |
| `RADIO_BUS_PORT` | `8765` | 没给 `RADIO_BUS_URL` 时的简写 |
| `RADIO_BUS_TOKEN` | 空 | **只从这里读**。绝不进命令行（会落进 shell 历史与 `ps`），绝不进 URL（会落进代理与访问日志），只发 `X-Radio-Token` 头 |
| `RADIO_BUS_TIMEOUT_MS` | `2000` | 单次总线请求超时 |

端口 8765 与姊妹仓 rsba1-core 的 MCP 示例端口相同，同机跑要错开一个。

## 工具面（八件）

| 工具 | 总线端点 | 参数 | 输出要点 |
|---|---|---|---|
| `get_state` | `GET /api/state` | — | 总线字段原样（含 `ptt_phase`）+ `freq_mhz` + `state_age_ms` |
| `get_spectrum` | `GET /api/spectrum` | `max_points` 1..512，默认 64 | 按桶取最大折叠后的谱 + `peak_bin`/`peak_hz`/`peak_db`/`noise_floor_db` + `axis` |
| `link_health` | `GET /api/state` + 两次 `GET /api/spectrum` | `window_ms` 0..2000，默认 200 | `bus_reachable`/`spectrum_advancing`/`frames_in_window`/`measured_window_ms`/`state_age_ms`/`warnings`；帧率用**实测间隔**算，不拿 `window_ms` 当分母 |
| `tune` | `POST /api/control` `tune` | `freq_hz` 正整数 | `tuned`/`freq_hz`/`freq_mhz`/`ptt_phase`；被门禁拒 ⇒ `isError`，频率不变 |
| `set_mode` | `POST /api/control` `set_mode` | `mode` ∈ LSB/USB/AM/CW/RTTY/FM/WFM | `mode`/`requested`；名字不认识 ⇒ `isError` |
| `ptt` | `POST /api/control` `ptt_on`/`ptt_off` | `action` ∈ on/off，`confirm` 布尔 | 见下面「两击门禁」 |
| `log_qso` | `POST /api/qso` | `callsign` 必填；`freq_mhz`/`mode`/`rst_sent` 可选 | `logged`/`count`/`qso`；**队列只在内存，进程重启即失** |
| `export_adif` | `GET /api/qso.adif` | — | `adif` 文本 + `records`（数 `<EOR>`）。不是磁盘上的 HamLog 库 |

注解按 MCP 的 hint 语义标：三个读工具 + `export_adif` 是 `readOnlyHint`；
`tune`/`set_mode`/`log_qso` 不是只读但 `destructiveHint:false`；
**`ptt` 标 `destructiveHint:true`** —— 它能把发射机推上空。

**横轴不是 RF。** `power_db[i]` 对应的频率是 `bin_hz × i`，即当前制式解调出来的
**音频基带 0..fs/2**（`fs=48 kHz` ⇒ 0..24 kHz）。给 agent 的 `axis.kind` 就是
`audio_baseband_hz`，别让它以为这是射频刻度。

## 两击门禁（这一层不许打折）

**一次 `tools/call` 最多发一次总线动作。** 所以单次调用不可能从 RX 直接发射：

1. `ptt(action="on")` → 只武装。返回 `ptt_phase:"confirm_pending"`、`transmitting:false`、
   `bus_presses_sent:1`，并告诉你下一步要么带 `confirm=true` 再调一次、要么 `action="off"` 取消。
2. `ptt(action="on", confirm=true)` → 仅当门禁**已经**处于待确认时才按第二击进 TX。
3. `ptt(action="off")` → 直接回 RX，不借道 TX，也不需要 `confirm`（带了就报错）。

判断依据是总线 `/api/state` 的 **`ptt_phase`**（门禁相位），不是 `ptt_on`（电台方向）：
后者在 `confirm_pending` 时仍是 `false`，拿它判会把「已武装」读成「空闲」。
总线版本旧到没有 `ptt_phase` 字段时，`ptt` **直接拒绝执行**，不猜。

被拒的几种情形都回 `isError` 且**一个总线动作都不发**：未武装就带 `confirm`、
已武装却不带 `confirm`、已经在发射还按 `on`。非业余段即使两击齐了也会被拦，
结果里的 `warnings` 会区分「确认窗过期，这一下只是重新武装」和「频率可收听但不在业余段」。

## M2 顺手修掉的五个真缺陷（都在总线/会话侧，每条都有回归用例）

前四个是写出口接上 MCP 才暴露的 —— PWA 里没人调 `ptt_off`，MCP 是第一个真用户：

1. **`ptt_off` 连按两下**：从 RX 出发是 `Rx→ConfirmPending→Tx`，「关发射」把自己按成了发射；
   从 TX 出发则停在 `ConfirmPending`（还 armed）。改成会话层新增的 `requestRxNow()`：
   待确认只清门禁不发 PTT ON，真在发才补一次 PTT OFF。
2. **`tune` 调了两次 `tuneTo`**：发两帧设频、被拒时闪两次提示，而且第二次失败会把
   `result` 整个换成 `{"error":"rejected"}`，连 `tuned`/`target_hz` 都没了。改成一次调用。
3. **`ptt_on` 的相位说假话**：从 Tx 按一下（实际关掉了）也回 `confirm_pending`。
   改成回读门禁真实相位。
4. **`/api/state.ptt_on` 永远是 false**：`readPttFrame` 定义了却从没发出去过，
   写 `0x1C` 只回 ACK 不回状态。`pollRadio()` 补上读帧 —— 否则总线对着正在发射的电台报「RX」。
5. **设频回读没到就两击，拿旧频率放行发射**（唯一一个是安全洞，真总线上实测
   「调到 100 MHz 后立刻两击」**12 轮漏放 10 轮**）。根因：`tuneTo` 发完设频帧就返回，
   `store.freq_hz` 要等下一轮 250 ms 轮询才更新，而确认分支拿 `store` 判业余白名单。
   修法：会话记住「已发出、还没回读到」的 target（`pending_freq_hz_`），
   发射前**两个频率都得在业余段**才放行；回读追上或电台 NAK 掉设频时清掉 pending。
   手动两击与 FT8 定时发射（`beginScriptedTx`）两条路都走这个校验。

第 5 条的教训值得单说：**原来那条「非业余段拒绝」用例是绿的，因为它 `settle()` 等了回读**，
恰好把真正危险的窗口绕过去了。现在补了不等回读的会话级用例（不跑事件循环，窗口是确定性的）
和一条总线级用例。变异对照：把校验改回「只看 store」，三条新用例全红，
总线那条实测 `pttWrites(true) == 1`（真发了 PTT ON 帧）；修好后同一探针 0/12。

## 安全边界（适配器一侧）

1. 适配器不自己判频率合不合法、不自己编排两击：门禁全在总线/会话侧，
   这边只保证「一次调用一次动作」并把拒绝原样透出去。
2. 总线地址非环回而 `RADIO_BUS_TOKEN` 为空 ⇒ **构造期直接拒绝**，和 `bus_server.cpp`
   「非环回绑定必须带 token」同一条规矩，客户端再挡一道。
3. 日志只写 stderr：stdout 的每一个字节都必须是协议帧（一行 `print()` 就能毁掉整条连接）。
4. stdio 强制 UTF-8：Windows 上 stdout 进管道会退到本地代码页（本机 GBK），中文工具描述
   会先变乱码、再变对端解不开的帧。`force_utf8_stdio()` 是这条坑的补丁，
   `tests/test_server.py::SubprocessTransportTests` 是真起子进程盯着它。
5. 工具层失败回 MCP 的 `isError` 结果（带人话），不回 JSON-RPC 协议错误 ——
   模型要能读懂「为什么没成」才能自己改下一步。401 的文本里不含 token 值。
6. 封装里**不重试、不补发**。`bus_client` 只做一次 HTTP 调用；偷偷补第二击等于把门禁拆了，
   `test_one_call_is_one_request_no_hidden_retry` 和 Python 侧变异对照盯着这条。

## 已验 / 未验

已验（本机 Windows 10，MSVC 2022 + Qt 6.8.3 + CPython 3.13.2）：

- C++ 全量 **ctest 228/228 通过**（27.8 s），含新增 3 条门禁用例
- 纯标准库 Python 单测 **57 条通过**：
  `python -m unittest discover -s tools/mcp_radio/tests -t tools/mcp_radio/tests`
  （假总线 + 假 bus，不碰 Qt、不联网）
- 真总线冒烟 **56 项全 PASS**：`radio-suite --headless --mock --bus 8790` 起着，
  子进程 stdio 走 `initialize → tools/list → 三读 → 被拒的路 → 两击发射 → 非业余段拦截 → QSO/ADIF`。
  会改电台状态的那一段要显式 `--allow-tx`（默认只跑读 + 被拒的路，对着真机也安全），
  跑完把频率/制式还原。**别对着接了天线的真机加这个开关。**
- 变异对照：
  - C++ 把 `bus_server.cpp` 退回 M2 之前 ⇒ 7 个用例 / 9 条断言红
  - C++ 把发射校验改回「只看 store」⇒ 3 条新用例红（`pttWrites(true)` 展开成 `1 == 0`）
  - Python 让适配器在一次调用里补第二击 ⇒ 4 条红，其中
    `test_first_call_only_arms_and_sends_exactly_one_press` 展开成 `'tx' != 'confirm_pending'`
  - 真总线漏放探针：修前 10/12，修后 0/12

未验（别把上面的绿读成这些也绿了）：

- **真电台**：IC-705 不在手，全部对着 mock。第 5 条那个 250 ms 窗口在真 UDP 链路上
  只会更长（往返 + 电台处理），不会更短。
- **UI**：这一轮没起实窗、没点过任何按钮。`requestRxNow()` 只被总线和用例调过，
  GUI 的 PTT 按钮仍走 `pttButtonPressed()`，没在真窗里复验。
- **Linux / macOS**：只在本机 Windows 构建跑过，三平台交给 CI。
- 非环回 + token 的真总线（见下面「缺口」第二条，命令行根本还没开这个口子）。
- 第三方 MCP 宿主的兼容性：只按 2026-07-28 规范自测，没拿 Claude Desktop 点过。
- 设频被电台 NAK 后清 pending 那条分支：mock 的 RX 范围与 `authorizeSetFreq` 完全同口径，
  构造不出「会话放行但电台 NAK」的设频，所以这一行没被用例覆盖（方向是 fail-safe）。

## 缺口（都是总线侧的，不在这里绕过去）

1. `link_health` 只能证明**总线自己的 DSP 定时器**在跑；「电台链路是否活」在会话层
   （`RadioSession` 已有活性判定）还没进 `/api/state`，所以这个工具不许被读成「电台通了」。
2. `bus_server.h:5` 写着「非环回必须 `--token`」，但 `main.cpp` 的 CLI 只有 `--bus <port>`，
   没有 `--token`、也没有 `--bind` —— 这条安全规则目前从命令行不可达。要上远端先得补 CLI。
3. **命令已发 ≠ 回读已到**：`tune`/`ptt` 返回只代表总线收下了命令，`/api/state` 要等下一轮
   250 ms 轮询才反映出来。宿主紧接着读 state 会读到旧值，这是总线的真实语义。
   冒烟脚本用 `wait_state()` 等回读；第 5 条缺陷正是这个窗口造成的，已在会话侧堵住发射方向。
4. **Split 的发射频率校验仍看主 VFO**：`pttButtonPressed` 确认分支用的是 `store.freq_hz`，
   split 时真正的 TX 频率是另一个 VFO。这是 M2 之前就有的口径，本轮没动（`tuneSubTo`
   在 TX 中改 sub 频率时另有校验）。要修得先把「TX 频率 = 哪个 VFO」在会话层定清楚。
5. 计划文档里写的 `{action:"freq"}` 与实际实现不符：总线认的是 `tune`/`set_mode`/`ptt_on`/`ptt_off`，
   且**没有** `set_toggle` 动作。M2 按实际契约写，不照文档写；文档更正单独一条待办。

## 下一步

M3 Streamable HTTP（含 `-32022` 版本协商与鉴权口径）→
M4 与 rsba1-core 那份 MCP 的关系（合成一个门面 vs 并存两个，这条要拍）。