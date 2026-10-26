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
  - cmd envelope + 15s ping / 断线 5s 重连
  - 四指标测量点内建（FPS/帧间隔/PTT lifecycle 毫秒/音频帧计数丢包）
- 解码正确性单测锁定：`tests/test_engine_spike.cpp`
- 构建：`rs-build.bat spike-configure && spike-build` → build-spike/engine-spike.exe ✓
- 主构建 69/69 全绿（2026-10-26 补生产 wire、session、真实 socket/SQLite 测试后）

## §C/D/E（E 节已完成；C/D 等 B 解除）

- **E 节 ✅**（`b3dbb7e`）：`docs/engine/268-delta.md`——268 段落级修正清单
  （M1 自研协议段作废→274 四指标；M0/M3/M4/M5 实况对照；SafetyGate 升级为
  纵深防御二层）+ 引擎依赖声明。
- 上游 issue 文本定稿 ✅：`docs/upstream-rigplane-issue.md`（可粘贴正文）。
- C（engine_client 正式化）/ D（音频路径裁决）：等 B 解除（上游修 rigctld
  后端 unselected 轮询，或 rigplane 侧接受配置跳过）后执行。

## §附加（同批）：Voice Memory 原厂件（`840144a`）

原厂 chm 清单的 Voice Memory 件，纯本机完整可测：AudioIn 录音（60s 上限）→
wav 存 tools/voice/ → 列表 → AudioOut 播放 → 门禁编排发射（同 SSTV）。
右侧大按键列 VOICE（紫）。麦克风采集本机实测（61 帧/2s、电平非零；WASAPI
帧率 30fps vs 50 为协商偏差已知项）。UIA 验证 VOICE 按钮在位。

## §附加（同批 II）：HamLog 工作台 v2（`f8f7f4a`）

师傅点名：关闭键/最强查询/其他功能缺失。**对照原版源码**（Desktop/HamLog
programs/R1.0.0 的主窗口 2093 行 + 数据层）补齐：

| 原版功能 | 工作台 v2 |
|---|---|
| 四字段模糊搜索（Callsign/QTH/Device/Remarks） | ✓ search 重写为原版语义，placeholder 对齐 |
| LogDialog 完整字段编辑 | ✓ QsoEditDialog（新增/修改/以模板添加三用，字段集同原版） |
| 设置（my_* 四项） | ✓ 直写 settings 表（原版同 key） |
| QRZ.com 查询 | ✓ 浏览器跳转版 |
| 删除/关闭键/统计 | ✓ + QSL 卡债计数 |
| LoTW 上传/字体/代理 | TODO（原版独有服务，不做假件） |

HamLogDb 扩展：search 四字段/getById/update/remove/getSetting/setSetting，
读路径 const 化。**平移期教训**：AUTOMOC 对新头文件的编译晚于 target 增量——
声明未实现的 slot/丢 namespace/值成员前向声明等错误要到 ninja regenerate
才暴露（本轮连环三坑）。

## §4 依赖声明

- rigplane v3.0.0b20（PyPI --pre），MIT，github.com/rigplane/rigplane-core
- sidecar 启动方式（真机时）：`python -m rigplane --model IC-705 --host <IP>
  --user linnan --pass-file pw.txt web --listen 127.0.0.1 --port 8490`
