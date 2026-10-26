# 上游 issue 文本（rigplane）：rigctld 后端无法满足 IC-705 profile 的 unselected 观测

> 用途：提给 github.com/rigplane/rigplane-core（工单硬边界：不改其仓，走 issue/PR）。
> 以下为可直接粘贴的 issue 正文（英文正文 + 复现细节），本文件留档。

---

**Title**: rigctld backend cannot satisfy IC-705 profile's unselected-VFO polling — web listener never starts

**Version**: 3.0.0b20 (PyPI --pre, 2026-10-25)
**OS**: Windows 11 · **Python**: 3.13.2

## Summary

When running `rigplane web` with `--backend rigctld` against a single-receiver
hamlib rigctld server, the web listener never opens. The startup gate waits on
`initial state acquisition` for three observations that the rigctld backend
cannot produce:

```
initial state acquisition still waiting on
  global.tx_state.split,
  receiver.main.unselected.freq_mode.freq_hz,
  receiver.main.unselected.freq_mode.mode
```

The process stays alive (observation poller loops) but `/healthz` never accepts
connections (45 s polling at 5 s intervals: connection refused throughout).

## Root cause (source-level; rechecked against `main` on 2026-10-26)

- `rigs/ic705.toml:132-133` declares `receiver.main.unselected.freq_mode.*` as
  `polling_only` capabilities.
- `src/rigplane/backends/rigctld_client/radio.py:863-886` `get_freq` and
  `get_mode` only support the MAIN receiver through `_require_main_receiver`.
- There is no rigctld command mapping for the unselected VFO (hamlib's
  `x`/`get_rx_frequency` family or a VFO-argument form), so the three
  observations above can never be satisfied → `readyz` can never turn ready.

The LAN backend does not have this gap (native dual-VFO CI-V commands), so the
mismatch only surfaces on the rigctld backend.

## Reproduction

1. Start any hamlib `rigctld` (a minimal mock works — every polling command
   below answers correctly):
   ```
   f → 14270000 · m → USB\n2400 · s → 0\nVFOA · t → 0
   l RF → 0.500000 · u NB → 0 · f VFOB → <freq> · m VFOB → <mode>\n<pb>
   \get_split_freq → 0 · \get_powerstat → 1
   ```
2. `python -m rigplane --backend rigctld --host 127.0.0.1 --control-port 4532 --model IC-705 web --listen 127.0.0.1 --port 8490`
3. `curl http://127.0.0.1:8490/healthz` → connection refused indefinitely.

## Expected

Either (a) the rigctld backend maps unselected-VFO observations to available
hamlib commands (e.g. `f VFOB` form), or (b) profiles marked single-receiver
skip unselected observations when the backend cannot produce them, and the
startup gate releases (the "serving not-ready while the first connect keeps
retrying" semantics at web_startup.py already exist for the busy-hold case).

## Context

Found while wiring an external AGPL client (radio-suite) that uses the web API
as a sidecar. Happy to send a PR if a maintainer can point at the preferred fix
shape (backend command mapping vs. profile capability gating).

---

*（留档说明：提交时按上游 issue 模板增删；本仓硬边界=不改 rigplane 仓。）*
