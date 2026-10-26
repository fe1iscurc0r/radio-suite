# 上游 issue #4352（rigplane）：rigctld 后端无法满足 IC-705 profile 的 unselected 观测

> **状态：已提交 2026-10-26** → https://github.com/rigplane/rigplane-core/issues/4352
> （opened by fe1iscurc0r；正文与下方完全一致，已回读核对）
> 工单硬边界：不改 rigplane 仓，只走 issue/PR。本文留档最终正文与提交前复核证据。

## 提交前复核（2026-10-26）

同一套最小 rigctld mock、同一组参数，分别跑：

| 版本 | 结果 |
|---|---|
| `3.0.0b20`（PyPI 最新 `--pre`，即本地 sidecar 钉定版） | `/healthz` 45s 全程 connection refused |
| `main @ 134f8446`（2026-10-25 21:14Z，含 #4347 等 post-b20 修复） | 同上，日志签名逐字相同 |

实测日志（两版一致，节选）：

```
INFO waiting for initial state acquisition: 65 fields outstanding
INFO initial state acquisition: no answer for <62 paths> after 10s; serving without them
INFO initial state acquisition still waiting on global.tx_state.split,
     receiver.main.unselected.freq_mode.freq_hz, receiver.main.unselected.freq_mode.mode
... 每 5s 重复；/healthz 全程 refused
```

即 MOR-2749 的 10s 非关键降级是生效的（65→3），**卡住的只有三条被判定为
"safety-critical" 的路径**。复核环境：Windows 11、Python 3.13.2、mock 正确应答
`f/m/s/t/v/l RF/l AF/l PREAMP/l ATT/u NB/\chk_vfo/\get_powerstat/\get_split_freq/f VFOB/m VFOB`。

## 根因要点（源码行号均按 main @ 134f8446）

1. `rigs/ic705.toml:129-133`：unselected 频率/模式声明为 `polling_only`。
2. rigctld 客户端只读主接收（active）：
   - `backends/rigctld_client/radio.py:863-864`（`get_freq`）、`:885-886`（`get_mode`）
     都调 `_require_main_receiver(...)`，无 `f VFOB`/`x` 形式；
   - `backends/rigctld_client/observations.py:261-267`（`read_freq_mode_controls`）
     只读 active 频率/模式/滤波 + RF/AF；其外部 acquisition profile（`:102-215`）
     既没有 unselected 路径，也没有 `global.tx_state.split` capability。
3. `core/state_pipeline_contracts.py:597-612` 判定 critical 只看**字段名**：
   `_STARTUP_CRITICAL_FREQ_MODE_NAMES`（`:589-591`）= `_LIVE_TUNING_FIELD_NAMES`
   （`:495`）= `("freq_hz", "mode")`，**不看 slot/receiver**。于是
   `receiver.main.unselected.freq_mode.*` 被当成 active 调谐对被判 critical；
   `global.tx_state.split` 也按名（`:592-594`）判 critical。
4. `web/web_startup.py:585-601` 只对非 critical 路径做 10s 过期；
   critical 路径靠 `:602-620` 的 `consecutive_request_timeouts` 收口。但适配器根本
   不发这两类读请求，超时计数恒为 0 → 缺陷分支永不触发 → 无限等待。

LAN 后端不受影响：unselected 走原生 CI-V `0x25 0x01`/`0x26 0x01`
（`rigs/ic705.toml:1085-1091`）。

## 提交正文（与 #4352 一致）

---

## Summary

`rigplane web` with `--backend rigctld --model IC-705` never opens its listener against a
single-receiver hamlib `rigctld`: the startup gate waits forever on three observations the
backend never reads. Verified on **3.0.0b20** (latest PyPI `--pre`) and on **main @ 134f8446**
(2026-10-25 21:14Z, includes #4347), so this is not a release-only artifact.

```
initial state acquisition still waiting on
  global.tx_state.split,
  receiver.main.unselected.freq_mode.freq_hz,
  receiver.main.unselected.freq_mode.mode
```

The process stays alive, the observation poller keeps running, `/healthz` is refused for the
whole 45 s poll window.

## Reproduction

Minimal rigctld that answers every command the backend actually sends (`f`, `m`, `s`, `t`, `v`,
`l RF`, `l AF`, `l PREAMP`, `l ATT`, `u NB`, `\chk_vfo`, `\get_powerstat`, `\get_split_freq`,
`f VFOB`, `m VFOB`):

```python
# mock: f->14270000  m->USB\n2400  s->0\nVFOA  t->0  v->VFOA
#       l RF->0.500000  l AF->0.500000  l PREAMP->0.500000  l ATT->0.500000
#       u NB->0  \chk_vfo->CHKVFO 0  \get_powerstat->1  \get_split_freq->0
#       f VFOB->7074000  m VFOB->USB\n2400
python -m rigplane --backend rigctld --host 127.0.0.1 --control-port 14532 \
    --model IC-705 web --listen 127.0.0.1 --port 18490
```

Observed (b20, and identically on main):

```
INFO waiting for initial state acquisition: 65 fields outstanding
INFO initial state acquisition: no answer for <62 paths> after 10s; serving without them
INFO initial state acquisition still waiting on global.tx_state.split,
     receiver.main.unselected.freq_mode.freq_hz, receiver.main.unselected.freq_mode.mode
... repeats every 5 s; /healthz connection refused throughout
```

Note the MOR-2749 non-critical release does work (62 of 65 paths lapse at 10 s). Only these
three remain, and all three are classified safety-critical.

## Root cause

1. `rigs/ic705.toml:129-133` declares the unselected VFO pair as `polling_only`
   (`receiver.main.unselected.freq_mode.freq_hz` / `.mode`).
2. The external rigctld adapter only reads the active main pair and controls —
   `backends/rigctld_client/observations.py:261-267` (`read_freq_mode_controls` reads
   `get_freq`, `get_mode`, `get_rf_gain`, `get_af_level`); its external profile
   (`:102-215`) declares no unselected path and no `global.tx_state.split` capability. The
   client itself rejects non-main receivers: `backends/rigctld_client/radio.py:863-864`
   (`get_freq`) and `:885-886` (`get_mode`) call `_require_main_receiver(...)`. There is no
   read path for the unselected VFO at all (no `f VFOB` / `x` form).
3. The gate keeps waiting because of how "safety-critical" is decided:
   `web/web_startup.py:585-601` only expires **non**-critical paths after 10 s, and
   `core/state_pipeline_contracts.py:597-612` decides criticality by field **name**:
   `_STARTUP_CRITICAL_FREQ_MODE_NAMES` (`:589-591`) is `_LIVE_TUNING_FIELD_NAMES`
   (`:495`) = `("freq_hz", "mode")`, with no slot/receiver check. So
   `receiver.main.unselected.freq_mode.freq_hz` and `.mode` match the active tuning pair and
   are treated as safety-critical, even though they are the unselected slot.
   `global.tx_state.split` is critical by name as well (`:592-594`).
4. Because the adapter never issues a read for these paths, no answer-window timeout is
   recorded for them, so the MOR-2749 defect branch at `web/web_startup.py:602-620`
   (`consecutive_request_timeouts`) never fires either. The loop waits forever.

The LAN backend is unaffected because it reads unselected through native CI-V `0x25 0x01` /
`0x26 0x01` (`rigs/ic705.toml:1085-1091`); the mismatch is rigctld-only.

## Expected

Either shape (or both) would let startup complete:

- **(a) backend mapping**: read the unselected pair and split over hamlib — `f VFOB` /
  `m VFOB` (or the `x`/`X` split commands added in #4275/#4341 for split) — and expose them
  through the external acquisition profile so the gate's paths can be observed.
- **(b) gate/profile gating**: make `startup_critical_path` slot-aware so an `unselected`
  freq/mode path is not safety-critical, and/or mark the paths this backend cannot produce as
  unsupported in `build_external_rigctld_acquisition_profile`, so `unobserved_startup_paths`
  does not include them.
- **(c) safety net**: a critical path that the provider never even queues for reading should
  not block startup indefinitely — the critical-attempt accounting only covers paths that are
  actually read.

Happy to send a PR once you point at the preferred fix shape (backend mapping vs. profile
capability gating). Related: #4275, #4323, #4341, #4347, #4351, #3819, #2749.

## Environment

- rigplane 3.0.0b20 (PyPI `--pre`) and main @ 134f8446 (2026-10-25)
- Windows 11, Python 3.13.2, single-receiver hamlib `rigctld` mock (answers all polling
  commands correctly; no real radio involved)

---

*（留档说明：后续上游回复/修复进展追加在本文末尾；工单硬边界=不改 rigplane 仓。）*
