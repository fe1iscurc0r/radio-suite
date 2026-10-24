# SPDX-License-Identifier: AGPL-3.0-or-later

## src/bus

本模块按工单排期在 **M5** 落地：`radio-suite --headless --bus 8765`
（`GET /api/state`、`POST /api/control`、`GET /api/spectrum`、`GET /api/logbook`；
默认 bind 127.0.0.1，非环回必须 `--token`）。与 GUI 共用同一套 command handler。
当前为空占位，保持目录结构完整。
