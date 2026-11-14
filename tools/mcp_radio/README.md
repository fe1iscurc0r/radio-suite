# mcp_radio — radio-suite 的 MCP 门面（M1：只读三工具）

SPDX-License-Identifier: AGPL-3.0-or-later

把**本仓 HTTP 总线**转成一个 MCP server，让 agent（lumo / scratchpad / 任意 MCP 宿主）
能读电台状态。设计口径来自 `docs/research/mcp-and-phone-ui-2026-11-05.md` 的方案 C：
适配器只做转写，**不直连电台**。直连电台等于绕过 `RadioSession` 的唯一出口、
SafetyGate 的 PTT 两击门禁、非业余段拒绝与 10 分钟自动回 RX —— 那正是我们唯一差异点的反面。

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

## 工具面（M1 只有三件，全只读）

| 工具 | 总线端点 | 参数 | 输出要点 |
|---|---|---|---|
| `get_state` | `GET /api/state` | — | 总线字段原样 + `freq_mhz` + `state_age_ms` |
| `get_spectrum` | `GET /api/spectrum` | `max_points` 1..512，默认 64 | 按桶取最大折叠后的谱 + `peak_bin`/`peak_hz`/`peak_db`/`noise_floor_db` + `axis` |
| `link_health` | `GET /api/state` + 两次 `GET /api/spectrum` | `window_ms` 0..2000，默认 200 | `bus_reachable`/`spectrum_advancing`/`frames_in_window`/`measured_window_ms`/`state_age_ms`/`warnings`；帧率用**实测间隔**算，不拿 `window_ms` 当分母 |

**横轴不是 RF。** `power_db[i]` 对应的频率是 `bin_hz × i`，即当前制式解调出来的
**音频基带 0..fs/2**（`fs=48 kHz` ⇒ 0..24 kHz）。给 agent 的 `axis.kind` 就是
`audio_baseband_hz`，别让它以为这是射频刻度。

## 安全边界（适配器一侧不许打折）

1. M1 不发任何写请求：`/api/control` 一行代码都没有。发射类工具进 M2，且必须原样透出
   总线 `ptt_on` 的第一击 `confirm_pending`，由模型显式二次确认。
2. 总线地址非环回而 `RADIO_BUS_TOKEN` 为空 ⇒ **构造期直接拒绝**，和 `bus_server.cpp`
   「非环回绑定必须带 token」同一条规矩，客户端再挡一道。
3. 日志只写 stderr：stdout 的每一个字节都必须是协议帧（一行 `print()` 就能毁掉整条连接）。
4. stdio 强制 UTF-8：Windows 上 stdout 进管道会退到本地代码页（本机 GBK），中文工具描述
   会先变乱码、再变对端解不开的帧。`force_utf8_stdio()` 是这条坑的补丁，
   `tests/test_server.py::SubprocessTransportTests` 是真起子进程盯着它。
5. 工具层失败回 MCP 的 `isError` 结果（带人话），不回 JSON-RPC 协议错误 ——
   模型要能读懂「为什么没成」才能自己改下一步。401 的文本里不含 token 值。

## 已验 / 未验

已验（本机 Windows，Qt 6.8.3 + CPython 3.13.2）：

- 纯标准库单测 **26 条**：`python -m unittest discover -s tools/mcp_radio/tests -t tools/mcp_radio/tests`
  （假总线 + 假 bus，不碰 Qt、不联网）
- 真总线冒烟 **19 项全 PASS**：`radio-suite --headless --mock --bus 8790` 起着，
  子进程 stdio 走 `initialize → tools/list → get_state → get_spectrum → link_health`，
  并确认 `ptt` 工具不存在（-32602）、`prompts/list` 给 -32601
- 变异对照三处，每处恰好让一条用例变红：版本协商回退、频谱折叠的桶宽、401 文本泄 token

未验：

- 真电台（IC-705 不在手）；`get_state` 的字段来自 mock 会话
- 非环回 + token 的真总线（见下面「缺口」第二条，命令行根本还没开这个口子）
- 第三方 MCP 宿主的兼容性：只按 2026-07-28 规范自测，没拿 Claude Desktop 点过

## 缺口（都是总线侧的，不在这里绕过去）

1. `link_health` 只能证明**总线自己的 DSP 定时器**在跑；「电台链路是否活」在会话层
   （`RadioSession` 的活性判定）还没进 `/api/state`，所以这个工具不许被读成「电台通了」。
2. `bus_server.h:5` 写着「非环回必须 `--token`」，但 `main.cpp` 的 CLI 只有 `--bus <port>`，
   没有 `--token`、也没有 `--bind` —— 这条安全规则目前从命令行不可达。要上远端先得补 CLI。
3. 计划文档里写的 `{action:"freq"}` 与实际实现不符：总线认的是 `tune`/`set_mode`/`ptt_on`/`ptt_off`，
   且**没有** `set_toggle` 动作。M2 按实际契约写，不照文档写。

## 下一步

M2 写工具（tune/set_mode/ptt，两击门禁 + 非业余段拒绝的回归用例）→
M3 Streamable HTTP（含 `-32022` 版本协商与鉴权口径）→
M4 与 rsba1-core 那份 MCP 的关系（合成一个门面 vs 并存两个，这条要拍）。
