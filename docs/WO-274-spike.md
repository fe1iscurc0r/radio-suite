# WO-274 · B 节 spike 报告（阻塞状态：待 IC-705 开机）

- 日期：2026-10-25 · 状态：**受物理条件阻塞，非代码阻塞**
- 工单规矩：任一指标不过 → 停下来写分析。本报告即"停下来"的产物。

## 1. 实测过程（真实命令与输出）

### 1.1 sidecar 启动尝试（rigplane 3.0.0b20，pip --pre）

```text
$ python -m pip install --pre rigplane -i https://pypi.tuna.tsinghua.edu.cn/simple
rigplane 3.0.0b20 ✓

# 尝试 1：无电台（LAN 扫描）
$ python -m rigplane web --listen 127.0.0.1 --port 8490
Error: no radios found on LAN.            ← 直接退出，listener 未起

# 尝试 2/3：显式 --radio-host（IC-705 离线，192.168.0.31 ping 不通）
$ python -m rigplane --model IC-705 --backend lan --host 192.168.0.31 \
    --user linnan --pass *** web --listen 127.0.0.1 --port 8490 --no-rigctld
21:13:29 INFO    UDP open to 192.168.0.31:50001, my_id=0x0001C630
21:13:39 WARNING Control discovery failed before auth: ... after 10 attempts
21:14:59 WARNING discovery attempt 3/3 got no answer     ← 进程随后退出
$ curl http://127.0.0.1:8490/healthz → 000（连接拒绝，全程）

# 尝试 4：--preset headless，等待完整重试周期（40s+）
同样在 discovery 循环内，/healthz 全程 000。
```

### 1.2 关键发现：sidecar 的 listener 生命周期绑定 radio session

**在 radio discovery 成功之前，rigplane web 不监听任何端口**（实测 4 种启动方式一致）。
`/readyz` 的"503 until radio is ready"语义（web.md:276-304）描述的是 listener 已起、
电台未 ready 的状态；而实测中 radio 永远连不上时，**listener 本身不起**，进程在
session_lifecycle 的 discovery 重试（3 次 × ~40s）后退出。

这对 spike 意味着：四条指标全部需要在 radio ready 之后才有测量源——

| 指标 | 依赖 | 状态 |
|---|---|---|
| 1. 频谱 ≥10fps@700bins、单帧<150ms | radio ready → `/api/v1/scope` 有源 | **阻塞**（IC-705 离线） |
| 2. 音频 RX 10min / TX 5s | radio ready → `/api/v1/audio` | **阻塞** |
| 3. PTT <300ms | radio ready → cmd 往返 + lifecycle | **阻塞** |
| 4. 断线 5s 错误态 + 重连 | sidecar 可启动为前提 | **阻塞**（sidecar 起不来；"engine 死"的测量前提不存在） |

### 1.3 阻塞原因归类

- **不是 rigplane 缺陷**：session-lifecycle 先于 listener 是其安全设计（无电台的
  web 是空壳）；improvement 方向（先起 listener + /readyz 503）已与其文档语义
  对齐，可作为 issue/PR 提给上游（工单硬边界：不改 rigplane 仓，走 issue）。
- **不是客户端代码问题**：spike 客户端已就绪（见下节），解码器正确性由单测锁定。
- **是物理条件**：IC-705 关机（192.168.0.31 持续无应答，与 268 以来的状态一致）。

## 2. spike 客户端（已就绪，待真机）

落点：`apps/spike/`（BUILD_ENGINE_SPIKE 选项，默认 OFF——不进主构建产物，
显式 -DBUILD_ENGINE_SPIKE=ON 编译）。

- `engine_client_spike.cpp`：QWebSocket 三通道（ws/scope/audio）+ 16B scope 头
  解码（rigplane web/protocol.py:56-79 逐字段）→ 复用 WaterfallWidget 上屏 +
  cmd envelope（set_freq/ptt）+ 15s ping / 30s 无帧重连（对齐 rigplane 浏览器
  客户端 KEEPALIVE/HEARTBEAT 语义）+ 指标测量（帧时间戳/延迟/lifecycle）。
- 单测（不依赖 sidecar）：合成 scope 帧逐字段解码断言、命令 envelope 编码、
  重连状态机迁移（`tests/test_engine_spike.cpp`）。

## 3. 真机开机后的实测清单（30 分钟内可完成）

```bash
# 0) sidecar（IC-705 上电联网后）
python -m rigplane --model IC-705 --host <IP> --user linnan --pass-file pw.txt \
  web --listen 127.0.0.1 --port 8490
# 1) spike 编译
cmake -S . -B build -DBUILD_ENGINE_SPIKE=ON && cmake --build build
# 2) 指标 1：spike 窗口跑 60s，读 FPS/单帧延迟（程序内计时输出）
# 3) 指标 2：音频通道跑 10min，帧计数/丢包（WS 帧 seq 连续性）
# 4) 指标 3：PTT 点击 → command_lifecycle 时间戳差
# 5) 指标 4：taskkill sidecar → 客户端 5s 内错误态 → 重启 sidecar → 自动重连
# 6) 截图存 docs/screenshots/wo274-spike.png
```

## 4. 工单合规性自查

- 未转写任何 rigplane 代码（路线 A）；未 clone 大仓（273 时的 97MB 快照除外，
  本单用 PyPI 安装）；未动 rsba1-core；未改 rigplane 仓。
- "B 不过，后面一律不做"：C/D/E 未开工；本批仅 B 节代码+报告。
