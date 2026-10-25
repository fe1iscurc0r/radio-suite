# rsba1-core 与 rigplane、radio-suite 的三方关系（师傅 2026-10-25 定案）

> 师傅原话精神："radio-suite 是新仓库，rsba1-core 是旧仓库；radio-suite 独立于
> rigplane 项目路线；**引擎是 rigplane**；**rsba1-core 准备与 rigplane 合并**。
> 两个不一样。做完继续写代码。"

## 三方关系图

```
rsba1-core（MIT，旧仓 fe1iscurc0r/rsba1-core）
    │  RS-BA1 协议 Python 实现（IC-705 E2E 验证）
    │  ★ 准备与 rigplane 合并（贡献为 rigplane 的 RS-BA1 后端）
    ▼
rigplane（MIT，引擎）──────────── sidecar REST/WS/音频 ────────────┐
    多机型 · 音频 · 频谱 · rigctld · webUI                          │
                                                                    ▼
                                    radio-suite（AGPL，新仓，本项目）
                                    = C++ 客户端（apps/desktop）+ HamLog
                                      + APRS + SSTV + bus + agent 生态
```

## 各自的边界

| 仓 | 角色 | 本仓对待它的方式 |
|---|---|---|
| **rsba1-core**（旧） | RS-BA1 协议实现，**将并入 rigplane** | `libs/protocol/rsba1-core/` 跟随副本（fixtures/桥的来源）；合并完成后此目录退役，引擎直接原生支持 RS-BA1 |
| **rigplane**（引擎） | 多机型引擎（sidecar） | **不抄代码、不改其仓**；只按其 REST/WS 契约写客户端（274 路线 A）；改进走 issue/PR |
| **radio-suite**（新，本仓） | 引擎的客户端 + 价值层 | 协议层不自研（274 作废旧 transport 主管道）；HamLog/APRS/SSTV/bus/agent 集成是本体 |

## 对 273 集成图谱的修正

- 图谱 v1 里"kappanhang 音频移植进本仓协议层"保留为**对照实现**（270bedde），
  但主管道让位给 rigplane 引擎（274）。
- rsba1-core 的归宿从"本仓活子项目"修正为"**上游待并入 rigplane**"——本仓的
  `libs/protocol/rsba1-core/` 是跟随副本（fixtures 生成 + Python 桥 import 用），
  rsba1-core→rigplane 合并落地后退役。
- 本仓 SafetyGate 保留：引擎侧有 managed-transmit 闸，客户端二次确认是纵深防御。

## 同步与回归（rsba1-core 跟随副本）

```bash
# 上游 rsba1-core 有新提交（尤其它向 rigplane 合并的过程）→ 跟随：
git subtree pull --prefix=libs/protocol/rsba1-core \
  https://github.com/fe1iscurc0r/rsba1-core main --squash
python tests/fixtures/gen_from_reference.py   # fixtures 互锁再生成
cmake --build build && ctest --test-dir build --output-on-failure
```

许可不变：rsba1-core（MIT）→ rigplane（MIT）合并是同许可；radio-suite（AGPL）
只消费 rigplane 的运行时接口（sidecar），不复制其代码。
