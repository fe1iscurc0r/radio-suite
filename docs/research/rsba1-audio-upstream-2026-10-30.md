# RS-BA1 音频通路：上游语义对照与本机环回验证（2026-10-30）

> 范围：RS-BA1 V2 三信道中的 **50003 音频**（RX 重排 / TX 打包）与 **50001/50002
> 握手编排**。目的不是替代真机验收，而是把「协议语义是否与上游一致」变成可在
> CI 里重复执行的断言。真机验证仍按 `docs/HANDOVER.md` §5.5 搁置。

## 1. 上游基准

| 来源 | 用途 | 取值 |
|---|---|---|
| kappanhang（Go, MIT，IC-705 无线语音事实标准实现） | RX 抖动缓冲语义 | `audioRxSeqBufLength = 100ms`；`rxSeqBuf.init(length, 0xffff, 0, ...)` |
| rsba1-core（本仓 `libs/protocol/rsba1-core` 来源，MIT） | 握手报文逐字节基准 | `tests/fixtures/*.hex`，由参考实现生成，**IC-705 真机验证 2026-08-18** |
| kappanhang `sendPart1/2` | TX 音频包布局 | part1 `6c 05` + 1364 B，part2 `44 02` + 556 B，24 B 头，seq 在 `[6:8]` LE |

kappanhang 的 RX 行为（对照实现要点）：

1. 首个音频包确立基线 seq；
2. 乱序/缺口的包先缓存，缺口补齐后按序释放；
3. 缺口超过 100 ms 未补 → 放弃等待、请求重传并放行后续包；
4. 迟到（已被放弃）的包**不再投递**，避免把旧语音混进当前流。

## 2. 本仓实现与差异

`apps/desktop/core/audio_stream.h` 的 `AudioRxBuffer` 语义逐条对齐：

| 上游行为 | 本仓实现 | 证据 |
|---|---|---|
| 首包定基线 | `feed()` 首个合法包直接建立 `expected_` | `[audio]` 顺序流测试 |
| 乱序缓存、按序释放 | 未来包进 `pending_`，`drainContiguous()` 连续释放 | `[audio]` 102 先到、101 补齐 → 两块按序投递 |
| 缺口 100 ms 释放 | `flush(now)`：最早未来包距上次进度 ≥ `kReorderWindowMs` 才放行，缺口计入 `lostPackets()` | `[audio]` `flush(100)` 不释放、`flush(106)` 释放并记 2 丢包 |
| 迟到/重复丢弃 | `seqDistance(got, expected_)` 回绕比较 → `droppedPackets()` | `[audio]` 重复包与 0xFFFF→0→1 回绕 |
| 只认音频包 | ≥ 24+556 B 且前缀 `6c05`/`4402` | `[audio]` 600 B 垃圾、124 B 正确魔数（不够长）均忽略 |

**已知差异（有意保留）**：缺口时本仓不向电台发音频重传请求。串行信道已有
`0x01` 重传（`dispatchSerial`），音频信道未验证过来自重传请求的真机行为，
贸然发送的风险高于收益；100 ms 放行本身已保证音频流不无限卡住。真机复验时
若要补，应先抓包确认电台是否响应音频信道重传请求。

## 3. 假服务端（test-only）保真度

`tests/fake_rsba1_server.{h,cpp}`：一条线程持 3 张 `QUdpSocket`，绑 127.0.0.1
随机端口，模拟 ctrl/serial/audio 三信道。

**与真机逐字节一致的**（测试用例直接比对 fixtures）：

- 登录应答 `0x60`（`udp_login_resp_ok.hex`）：seq 字节、auth 6 B @26、结果码 @48；
- ConnectTrans 应答 `0x90`（`udp_connecttrans_resp.hex`）：新 SID @8/@12、auth @26、
  机型字符串 @64、ok @96；
- 鉴权应答 `0x40` 魔数 @21；`0xA8` 下推 168 B、reply id @66..82（依 `rsba1_proto` 解析器）。

**推定、真机未逐字节核对的**：

- 音频包 24 B 头中 `[6:8]` 以外的字段（`[16]=0x80`、`[18:20]=(seq-1)` BE、
  `[22:24]` 长度 BE）——按 kappanhang `sendPart1/2` 布局构造；
- serial 信道下行帧用全 LE wire 头（`serWireHeader`），与客户端
  `dispatchSerial` 的解析一致，但真机下行抓包未核；
- 三信道 server SID 取值（`0x0A0B0C0D` 等）为测试常量，真机由电台下发。

这些推定只影响「假服务端像不像真电台」，不影响「客户端实现是否符合协议」——
后者由 fixtures + `rsba1_proto` 解析器双向锁住。

## 4. 环回测试覆盖（`[rsba1][loopback]`，6 用例 / 61 断言）

| 用例 | 断言要点 |
|---|---|
| fixtures 对照 | 假服务端构造的登录/ConnectTrans 应答与 IC-705 fixtures **逐字节相等** |
| 握手编排 | `isOpen()`、`audioReady()`、日志含机型与「双通路就绪」、serial open 已到达 |
| CI-V 上行/下行 | `send()` 的 26 B 帧被服务端解出原样 payload；注入下行帧经 `poll()` 回调还原 |
| TX 音频线格式 | 1920 B → 2 包（24+1364 / 24+556）、SID 取自握手、seq 递进、全零帧不上线 |
| RX 顺序/乱序/丢包 | 顺序立即投递；13/14 先到缓存、12 补齐后按序；18 缺 15..17 时 100 ms 后放行；迟到 17 丢弃 |
| 拆除五件套 | serial close → audio disconnect → deauth → serial/ctrl disconnect，`disconnectCount()>=3` |

**仍不能证明的**：真机时钟/抖动下的重排收益、真实丢包率、音频设备端到端延迟、
电台是否接受音频重传请求。这些需要 IC-705 上机。

## 5. 工程发现：Windows 上 `QUdpSocket::waitForReadyRead` 会把 UDP socket 打死

假服务端最初用 `waitForReadyRead(2ms)` 轮询三信道，现象：

- 握手全部正常，但客户端握手后的第一个 CI-V 帧（26 B）**永远收不到**；
- 服务端线程仍在跑（心跳正常），三张 socket 却从 `BoundState(4)` →
  `ClosingState(6)` → `UnconnectedState(0)`，`localPort()` 变 0；
- 期间反复出现 `errorOccurred(DatagramTooLargeError=7)`，`readDatagram` 返回 -1
  （"Unable to receive a message"）。

在 socket 上设 `SIO_UDP_CONNRESET=FALSE` 无效，说明不是 ICMP 端口不可达那条路径；
改用 `hasPendingDatagrams()` + `sleep_for(1ms)` 非阻塞轮询后一切正常。结论：
**测试/服务端侧的 UDP 收包不要用 Qt 的阻塞等待**；生产客户端
`Rsba1UdpTransport::poll()` 本来就是非阻塞，不受影响。已在
`tests/fake_rsba1_server.cpp` 就地注释。

## 6. 接真机时的复验清单（增量）

1. 抓三信道报文，核对本文 §3「推定」三项；
2. 弱信号/高抖动场景下观察 100 ms 放行与丢包计数是否符合听感；
3. 确认电台是否响应音频信道重传请求，再决定是否补发；
4. 长通话（>10 min）下 50001 周期再鉴权（60 s）是否被接受。
