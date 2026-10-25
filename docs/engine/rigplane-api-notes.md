# rigplane API 落图（WO-274 · A 节）

- 依据：逐份实读 `rigplane-core/docs/api/{web,scope,audio,public-api-surface,rigctld}.md`
  + `command-catalog.md` + `src/rigplane/web/protocol.py`（wire 格式源码），非 README 转述
- 版本背景：v3.0.0b20（2026-10-25 实查），MIT
- 本仓 v3.0.0b20 克隆快照：`D:/tmp/rigplane`（spike 用 `pip install rigplane`，见硬边界）

## 0. 重大契约事实（影响客户端设计，全部有出处）

1. **应用层认证已废除**：web.md:27-49「Bearer headers and query tokens no longer
   authenticate requests」——`--auth-token` 已在参数解析期报错，`RIGPLANE_AUTH_TOKEN`
   被忽略。**边界 = 监听器本身 + Host allowlist**（MOR-2880/2881）：Host 头须为
   IP 字面量/单标签名/.localhost/.local/.home.arpa/.internal，否则 HTTP 421、WS
   升级前 421。→ C++ 客户端**不需要 token 逻辑**，只需要保证 Host 头合法（QWebSocket
   默认发目标主机名，IP 直连即合法）。
2. **无 token ≠ 无会话**：多客户端走 subprotocol `rigplane-controller-v1.<key>`
   （web.md:189）；首条 `/api/v1/ws` 为 primary，其余 auxiliary。HTTP 可带
   `X-RigPlane-Controller: <key>` 代表会话。
3. **WS 高频 set_* 命令合并**（web.md:1369+，MOR-1427）：50ms 窗内同名命令
   last-value-wins，被替换帧立即回 `{"superseded":true}`；**入队 ACK `ok:true`
   不代表电台接受**，真实结果走 `command_lifecycle` 事件帧（`failed` 时 NAK）。
   → C++ 客户端对 set_* 必须等 lifecycle 才算落定。
4. **可管理发射**（managed-transmit）：`GET /api/v1/managed-transmit` +
   `POST /managed-transmit/command`（latched transmit / force-off）+ `PUT /tot`。
   这是 rigplane 侧的 TX 安全视图（工单 §A 问的 tx_safety 语义）；**本仓 SafetyGate
   保留在客户端侧双保险**（见 §4 与 268 硬约束 2 不冲突——门禁在客户端，引擎另有闸）。

## 1. radio-suite 最小接口集合

稳定性分级：**稳定**（`/api/v1` stable 面，web.md:9-25 契约保证）/
**实验**（文档标注 diagnostic/experimental 或 Pro 面）/**未文档化**（仅源码）。

| # | 接口 | 用途（radio-suite） | 稳定性 | 出处 |
|---|---|---|---|---|
| 1 | `GET /healthz`、`GET /readyz` | sidecar 存活/就绪探测（重连状态机依据） | 稳定 | web.md:264-304 |
| 2 | `GET /api/v1/state` | 状态单源快照（freqHz/mode/filter/PTT/radioHealth，ETag+revision） | 稳定 | web.md:1148-1204 |
| 3 | `WS /api/v1/ws` | ① 命令通道（cmd envelope）② 状态更新流 ③ command_lifecycle | 稳定 | web.md:1289-1400 |
| 4 | `WS cmd set_freq {freq,receiver}` | 调谐 | 稳定 | command-catalog.md:82 |
| 5 | `WS cmd set_mode {mode}` | 模式（"USB"/"FM"/"CW"…profile modes[]） | 稳定 | command-catalog.md:85 |
| 6 | `WS cmd ptt {state}` | PTT（含 ptt_on/ptt_off；只读模式 403） | 稳定 | command-catalog.md:118-120 |
| 7 | `WS cmd set_att/set_nb/set_nr/set_nb_level/...` | 功能开关（对应本仓 0x11/0x16 族） | 稳定 | command-catalog.md:132-144 |
| 8 | `WS /api/v1/scope`（二进制） | 硬件频谱帧流（0x27 组帧后） | 稳定 | web.md:176, protocol.py:56-79 |
| 9 | `WS /api/v1/audio`（二进制+JSON 控制） | RX 播放 / TX 采集（8B 头 + Opus/PCM16） | 稳定 | web.md:1300-1310; audio.md |
| 10 | `WS /api/v1/audio-scope` | 音频 FFT 频谱（无硬件 scope 的替代） | 稳定 | web.md:176,1284 |
| 11 | `POST /api/v1/commands`（HTTP） | 非订阅场景的单命令（回答即执行完） | 稳定 | web.md:362-423 |
| 12 | `POST /api/v1/civ/transaction` | 原始 CI-V 逃生门（自研协议层的过渡桥） | 稳定 | web.md:424 |
| 13 | rigplane 自带 rigctld（4532） | WSJT-X/fldigi **直连引擎**（radio-suite 自研 rigctld 的替代位） | 稳定 | rigctld.md（`rigplane serve`） |
| 14 | `POST /api/v1/transport/webrtc/offer|ice-candidate` | 音频低延迟可选项（D 节裁决 D2） | 实验（gated） | web.md:207-236,153 |

**未文档化**（源码级，spike 不依赖）：`web/_delta_encoder.py`（频谱增量编码——
工单点名注意；协议层面 scope 帧是"16B 头+pixels"整帧，delta 编码若用于 WS 需要
spike 实测确认；protocol.py:56 的 encode_scope_frame 是整帧基准）。

## 2. Wire 格式（客户端解码依据，源码级）

### 2.1 频谱帧（/api/v1/scope，protocol.py:56-79 = 16B 头 + pixels）

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 1 | msg_type | `0x01` SCOPE |
| 1 | 1 | receiver | 0=MAIN 1=SUB |
| 2 | 1 | mode | 0=center 1=fixed 2=scroll-C 3=scroll-F |
| 3 | 4 | start_freq_hz | LE uint32 |
| 7 | 4 | end_freq_hz | LE uint32 |
| 11 | 2 | sequence | LE uint16（服务端包序，断流检测用） |
| 13 | 1 | flags | bit0 = out_of_range |
| 14 | 2 | pixel_count | LE uint16 |
| 16.. | N | pixels | 幅度 0x00–0xA0（IC-7610 实测 689px/burst、15 seq；scope.md:58-61） |

### 2.2 音频帧（/api/v1/audio，8B 头 + payload）

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 1 | msg_type | `0x10` RX / `0x11` TX |
| 1 | 1 | codec | `0x01` Opus / `0x02` PCM16 |
| 2 | 2 | sequence | LE uint16 |
| 4 | 2 | sample_rate | **Hz/100** LE uint16（48000→480） |
| 6 | 1 | channels | 1 mono / 2 stereo |
| 7 | 1 | frame_ms | **advisory**（实际时长须由 payload 推，web.md:1310-1318） |

TX 语义（audio.md:279,342）：引擎侧 TX 线格式 = **PCM_1CH_16BIT**（1920B/20ms
@48k，与 kappanhang 移植件一致）；Opus 仅对支持的端点。RX 可 Opus 或 PCM16
（audio_start JSON 控制，`/api/v1/audio` 的 `audio_config`）。

### 2.3 命令 envelope（/api/v1/ws 与 POST /commands 同构）

```json
→ {"type":"cmd","id":"42","name":"set_freq","params":{"freq":14074000,"receiver":0}}
← {"type":"response","id":"42","ok":true,"result":{...}}          // 入队 ACK
← {"type":"command_lifecycle","commandId":"42","state":"failed"}  // 真实结果
```

## 3. spike 四硬指标对应的接口映射（B 节）

| 指标 | 用到的接口 | 测量点 |
|---|---|---|
| 频谱 ≥10fps@700bins、单帧<150ms | WS /api/v1/scope | 帧间时间戳 + sequence 连续性 |
| 音频 RX 10min 不断流 | WS /api/v1/audio（PCM16 优先，免 Opus 解码依赖） | get_audio_stats 等价物=帧计数/丢包 |
| PTT <300ms | WS cmd `ptt` + `command_lifecycle` | 点击时间戳→lifecycle 到达 |
| 断线 5s 错误态+自动重连 | /healthz 轮询 + WS close 信号 | kill sidecar 计时 |

## 4. 与 273 已交付件的处置（边界定案）

| 273 交付件 | 处置 | 理由 |
|---|---|---|
| 自研 `transport_udp`（RS-BA1 协议） | **降级为对照实现**（保留不删）——spike 期 A/B 对照、rigplane 故障时热备 | 已真机级验证，弃之可惜；但不作为主路 |
| 自研 rigctld（8238339） | **降级为对照**；主路用 rigplane 自带 rigctld（rigctld.md `rigplane serve`） | 避免双实现漂移（工单硬边界同款理由） |
| kappanhang 音频移植件（audio_stream.*） | **保留**——其 TX 打包/重排语义与 rigplane TX 线格式同源（PCM_1CH_16BIT 1920B/20ms），可作为 D1 直连 50003 的对照 | 真机排障用 |
| SafetyGate | **保留并加固**：rigplane 有 managed-transmit 闸，客户端 SafetyGate 继续作为 UI 层二次确认（268 硬约束 2） | 纵深防御 |
| HamLog/bus/APRS/SSTV | 不变（radio-suite 的本体价值层） | C3 差异点 |

## 5. 引擎依赖声明（工单 §许可要求）

- rigplane v3.0.0b20（main @ 2026-10-25 实查，4,614 commits），**MIT**，
  https://github.com/rigplane/rigplane-core，`pip install --pre rigplane`
  （stable 通道为 2.x；3.0 功能面以 --pre 为准）
- radio-suite（AGPL-3.0）以 **sidecar 子进程**方式调用（路线 A），不 vendor、
  不复制其 Python 代码；MIT 版权行与 LICENSE 链接保留于本文件与 NOTICE。
