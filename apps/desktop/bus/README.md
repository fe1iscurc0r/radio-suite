# SPDX-License-Identifier: AGPL-3.0-or-later

## apps/desktop/bus

`radio-suite --headless --bus 8765` 的本机集成入口，与 GUI 共用 `RadioSession` 命令出口。

当前实现：

- HTTP `GET /api/state`：实时电台状态快照；
- HTTP `GET /api/spectrum`：总线侧 `SignalSynth` 合成谱，不是 RF/音频输入频谱；
- HTTP `GET /`：上述两个接口的轮询演示页；
- rigctld TCP：频率、模式及 rigplane 观测轮询所需命令。

默认 bind `127.0.0.1`；非环回绑定必须配置 token。HTTP token 可用 `?token=` 或
`X-Radio-Token`。当前没有 WebSocket、`POST /api/control` 或 `GET /api/logbook`。
