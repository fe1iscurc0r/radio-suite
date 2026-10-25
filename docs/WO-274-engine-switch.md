# WO-274 施工记录 · radio-suite 换引擎（rigplane sidecar + C++ 客户端）

> 分支：`feat/wo274-engine-switch`（含 WO-273 分支的提交历史——274 在 273 基础上继续）
> 顺序：A ✓ → B（受阻塞分析）→ 师傅确认后 C/D/E

## §0 路线裁决

**路线 A（sidecar + WebSocket/REST）**——工单默认裁决照抄。B（pybind11）未启用；
C（整仓转写）未做（工单明确不批）。

## §A API 落图（commit `0a13ab3` + 验收修正）

- `docs/engine/rigplane-api-notes.md`
- 五份 API 文档逐份实读（web 1444 行 / scope 190 / audio 423 /
  public-api-surface / rigctld 714）+ command-catalog 405 行 +
  `src/rigplane/web/protocol.py` wire 格式源码
- 验收输出（实跑）：
```
$ grep -c "docs/api/" docs/engine/rigplane-api-notes.md
16        （≥4 ✓）
$ grep -c "未文档化" docs/engine/rigplane-api-notes.md
2         （≥0 ✓；web/_delta_encoder.py + framed_ms advisory 补充）
```
- 关键契约（影响客户端设计）：
  1. 应用认证废除 → 客户端零 token 逻辑，Host allowlist 即边界
  2. WS set_* 50ms 合并 + 入队 ACK ≠ 电台接受 → 等 command_lifecycle
  3. managed-transmit = 引擎侧 TX 闸；本仓 SafetyGate 保留（客户端纵深防御）

## §B spike（commit `ebff2c7` + v2 `0a288d6`）—— **受阻塞，按工单规矩停下写分析**

- 完整分析：`docs/WO-274-spike.md`（v2：§1 lan 后端 + §2B self-hosted 全链）
- 结论 v2（师傅指示真机搁置后做的 self-hosted 替代路径）：
  - 我们的 rigctld 服务器补齐 rigplane 全部轮询依赖（level/func/split/VFO 族），
    直连 socket 实测 10 条命令全对——rigplane 兼容度实证；
  - 硬结论：rigplane **rigctld 后端不实现 unselected-VFO 轮询**
    （`get_freq` 只支持 MAIN），而 IC-705 profile 声明 unselected 可轮询 →
    initial state acquisition 永不完成 → **listener 永不开放**（45s 逐 5s
    轮询 /healthz 全程 000 实证）。属 rigplane 空缺，issue/PR 上游；
  - 四指标无真机判定不可达。解除条件二选一：上游修复，或 IC-705 开机（lan
    后端一切就绪）。
- spike 客户端已就绪（`apps/spike/`，BUILD_ENGINE_SPIKE 默认 OFF）：
  - QWebSocket 三通道 + scope 16B 头解码（逐字段对照 protocol.py:56-79）
  - cmd envelope + 15s ping / 30s 无帧重连（对齐 rigplane 前端语义）
  - 四指标测量点内建（FPS/帧间隔/PTT lifecycle 毫秒/音频帧计数丢包）
- 解码正确性单测锁定：`tests/test_engine_spike.cpp`
- 构建：`rs-build.bat spike-configure && spike-build` → build-spike/engine-spike.exe ✓
- 主构建 56/56 全绿（ctest 0xc0000135 = Qt DLL PATH，rs-build.bat 已修）

## §C/D/E（未开工——工单：B 过后才做）

- C 适配层骨架 `libs/engine_client/`：spike 的解码器/重连状态机正式化位。
- D 音频路径：倾向 **D1 WS 二进制 PCM**（Qt QAudioSink 直喂；Windows 零额外驱动
  ——D3 虚拟声卡需 VB-Cable 是分发包硬伤；D2 WebRTC 引栈重）——spike 实测后定稿。
- E 268 scope 修正 delta：`docs/engine/268-delta.md` 待写（要点已有：协议/音频/频谱
  自研段落作废→rigplane 引擎；C++20/Qt6 保留）。

## §4 依赖声明

- rigplane v3.0.0b20（PyPI --pre），MIT，github.com/rigplane/rigplane-core
- sidecar 启动方式（真机时）：`python -m rigplane --model IC-705 --host <IP>
  --user linnan --pass-file pw.txt web --listen 127.0.0.1 --port 8490`
