# 268-delta：WO-274 换引擎后对 268 工单的修正清单（E 节）

- 原则（工单 §E）：只出 delta，**不改 268 工单文件本身**；268 的 C++20/Qt6 结论保留
- 268 原文：`TRAE_WORKORDER_PROMPT_AGENT_268-RESERVED.md`；换引擎裁决：WO-274

## 需要改写的段落

| 268 段落 | 原文要点 | delta（274 后） |
|---|---|---|
| §三 目标架构（CMake+Qt6 的模块表） | `src/core` = CI-V 编解码、transport(udp/serial) 自研 | core 的 rsba1_proto/transport_udp/**降级为对照实现**（保留编译与测试）；主管道 = rigplane 引擎 sidecar（274 路线 A），C++ 客户端走 REST/WS |
| §三 线程模型 | core 一条 I/O 线程自管协议 | 引擎进程自管；radio-suite 侧只剩 UI 线程 + 音频回调线程（RtAudio） |
| §四 M1（协议层）验收 | CI-V 编解码/transport/多收单发自研验收 | 作废，替换为 274 B 节四指标（频谱 fps/音频连续/PTT 延迟/断线重连）——受 rigplane rigctld 后端空缺阻塞（见 WO-274-spike.md §2B） |
| §四 M2 前置 | pocketfft + 合成信号自产频谱 | 保留（瀑布控件仍在用）；真实频谱源切换为引擎 `/api/v1/scope`（spike 通过后） |
| §四 M5 总线 | 自研 `/api/state` 等 | 保留但**契约对齐**：rigplane 自带 web API（稳定面）成为引擎侧事实标准，本仓 bus 维持给 lumo/agent 的稳定出口（两者并存：bus=本仓价值层，engine API=引擎面） |

## 保留不动的段落

- §零 双形态（独立应用 + headless 总线可嵌入）——274 后更强：引擎也是 headless
- §一 C++20 + Qt6 选型（理由不变：瀑布渲染 + Widgets 四件套）
- §三 硬约束 2（安全门禁 C++ 侧唯一出口）：**升级**——引擎另有 managed-transmit 闸，客户端 SafetyGate 为纵深防御二层
- §四 M3 主窗口 / M4 日志（HamLog 共库）/ §五 不做清单 / §六 许可 / §七 交付格式

## 落地状态对照（2026-10-25 实况）

| 268 里程碑 | 原定内容 | 274 后实况 |
|---|---|---|
| M0 spike | 构建+CI-V mock+瀑布 | ✅ 完成（12+ 提交，56/56） |
| M1 协议层 | 自研 serial/udp + SourceManager | 🔄 **协议自研段作废**（对照实现保留）；等引擎 rigctld 后端修复（issue） |
| M2 DSP+频谱 | pocketfft+500 帧红线 | 🔄 瀑布/合成谱已上屏；真源切引擎待 spike |
| M3 主窗口 | 面板补全 | ✅ 大部分完成（模式/开关/旋钮/记忆/原厂视觉） |
| M4 日志 | QtSql+ADIF | ✅ 提前完成（HamLog 共库+工作台） |
| M5 总线 | --headless --bus | ✅ 提前完成（curl 验收）+ rigctld |

## 引擎依赖声明

rigplane v3.0.0b20（PyPI --pre，MIT，github.com/rigplane/rigplane-core）；
sidecar 运行方式见 `docs/WO-274-engine-switch.md` §4；上游空缺（rigctld 后端
unselected 轮询）阻塞 M1 替代验收，issue 文本见 `docs/upstream-rigplane-issue.md`。
