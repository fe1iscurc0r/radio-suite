# WO-273 追加裁决：集大成整合图谱（师傅 2026-10-25 拍板）

> 师傅原话精神："我是不介意集大成者的。你直接拿来抄就好了，然后标注一下协议哪一段是来自哪里的。"
> 本文件取代 C1 节"红海-不碰"的保守结论——**红海项目的处理方式从"不碰"改为"按许可合规地拿，逐段标注出处"**。
> 差异点（C3）不变：拿来的都是零件，"agent 可调 + QSO 落 HamLog + lumo 可嵌 + 双端薄壳"仍是我们的组装层。

## 一、许可兼容总表（什么能直接抄、怎么标）

| 上游 | 许可 | 并入路径 | 标注义务 |
|---|---|---|---|
| rsba1-core（自有） | MIT | subtree 进 libs/protocol | 保留 LICENSE 文件 + 文件头版权行 |
| **kappanhang** | **MIT**（实查更正：GitHub 标 NOASSERTION 系识别失败，LICENSE 全文 MIT, © 2020 Norbert Varga HA2NON / Akos Marton ES1AKOS） | 逐文件移植进 libs/protocol（Go→C++） | 文件头保留版权行 + SPDX:MIT + 来源 URL/commit |
| **rigplane-core** | MIT | Python→C++ 移植 + 架构参考；文档引用注明 | 同上 |
| **SSTVEncoder2** | Apache-2.0 | ✅ 已并入（commit 37e0362），NOTICE 已存 docs/sstvencoder2-notice.txt | 已合规 |
| **wfweb / wfview** | GPL-3.0 | 依 **AGPLv3 §13** 并入本 AGPL 仓：可复制/修改，整仓按 AGPL 分发 | 文件头：来源路径 + commit + 版权行 + "GPL-3.0 code incorporated under AGPLv3 §13" |
| pocketfft/RtAudio/Catch2 | BSD/MIT/BSL | ✅ 已 vendor | 已合规 |

红线不变：**MIT 上游的代码永远不进 rsba1-core 之外的 MIT 仓的反向操作不存在**；真正要防的是"把 wfweb 的 GPL 代码抄进任何 MIT 文件里"——移植 GPL 代码必须整文件进 AGPL 区（libs/ 下标注 gpl-origin 的目录），与 MIT 移植区物理分开。

## 二、逐段出处台账（抄什么 → 放哪 → 怎么标）

### 2.1 协议层（libs/protocol/）

| 来自 | 段落/文件 | 拿什么 | 落点 | 标注模板 |
|---|---|---|---|---|
| rsba1-core | `src/rsba1/serial/*.py` | RS-BA1 V2 编解码参考（我们 C++ 已字节级复刻，此为权威对照） | 已在 vendor/ | 保留原样 |
| kappanhang | `client.go` / `audio.go` / `rtcp.go` | **RS-BA1 音频通路**：u-law 编解码、音频帧序号/重排、RTCP 控制字节——正好补 rsba1-core 的 50003 真空 | `libs/protocol/rsba1/audio_*.cpp` | `// Ported from kappanhang audio.go @ <commit> (MIT, © 2020 HA2NON/ES1AKOS)` |
| rigplane | `rigplane/backends/icom_lan/*.py` | Icom LAN 帧状态机（wfview 式 clean-room 实现，含重连/超时策略） | `libs/protocol/icom_lan/` | `// Ported from rigplane-core <path> @ <commit> (MIT, © KN4KYD)` |
| rigplane | `rigplane/profiles/` | 机型能力档案结构（IC-705/7300/7610/9700 + Yaesu FTX-1） | `libs/protocol/profiles/` | 同上 |
| rigplane | `rigplane/rigctld/` | rigctld 服务器协议（VFO 路由语义）——WSJT-X/fldigi 直接可用 | `libs/protocol/rigctld/` | 同上 |
| wfview/wfweb | `server.cpp` / `audio.cpp` | CI-V 串口与 LAN 并存架构参考（只读对照；GPL 代码若整文件移植则入 gpl-origin 区） | 视情况 | §13 标注 |

### 2.2 音频/频谱（libs/dsp/）

| 来自 | 段落 | 拿什么 | 落点 |
|---|---|---|---|
| kappanhang | `audio.go` | **u-law 编解码表 + 音频包序号策略**（0x27 频谱之外的另一真空） | `libs/dsp/audio_ulaw.cpp`（MIT 标注） |
| rigplane | `rigplane/dsp/scope*.py` | 0x27 分段扫描解帧参数（若其已实现；未实现则 wfview wireshark 包对照） | `libs/dsp/scope_frames.cpp` |
| wfweb | `spectrum*.cpp` | 浏览器频谱推送（WebSocket 二进制协议设计，GPL→§13） | `apps/web/`（阶段二） |

### 2.3 工作站/集成（apps/）

| 来自 | 段落 | 拿什么 | 落点 |
|---|---|---|---|
| wfweb | WSJT-X UDP 遥测对接、Direwolf 集成方式 | 数字模式对接（架构级参考 + 可整段移植） | `apps/desktop/` M3+ |
| rigplane | web UI 四皮肤交互设计 | PWA 信息架构参考（设计参考，不抄 Svelte 代码） | `apps/web/` 阶段二 |
| wfweb | ADIF 遥测/记录策略 | 对照我们 HamLog 共库（我们已有更好的） | 已做 |

## 三、标注规范（写进 CONTRIBUTING 并自动化）

1. **文件头模板**（移植文件必备）：
```cpp
// SPDX-License-Identifier: MIT  （或 GPL-3.0→本仓按 AGPL-3.0-or-later 分发）
// Ported from: <上游仓库>/<路径> @ <commit>
// Upstream copyright: © <年> <作者/呼号>
// Local modifications: <改动说明，无则写 "verbatim port">
```
2. **目录物理隔离**：`libs/**/gpl-origin/` 专用目录放 §13 并入的 GPL 文件（保持上游文件名，便于 diff 上游）；MIT/Apache 移植文件随常规目录。
3. **NOTICE.md 增加上游表**：每上游一行（仓库/许可/引入 commit/引入日期）。
4. **CI 守门（M3 加）**：脚本检查 gpl-origin/ 外不得出现 wfview/wfweb 的特征函数名，防 GPL 污染 MIT 链。

## 四、执行顺序（阶段三细化，师傅点头后）

1. **第一批（收益最大、全 MIT）**：kappanhang 音频通路移植（u-law + 重排）→ 直接补 rsba1-core/radio-suite 的 50003 音频真空；rigplane rigctld 服务器 → WSJT-X 即插。
2. **第二批**：rigplane Icom LAN 状态机 + profiles → 多机型支持（我们目前 IC-705 单机型）。
3. **第三批（GPL §13 区）**：wfweb 数字模式对接架构 → M3+。
4. 每批一个 PR，PR 描述列表：源文件路径 + commit + 版权人 + 改动说明（工单 §二的既定规约）。

## 五、对 C3 差异点的影响

不变，反而更强：**"集大成"解决的是"零件从哪来"，差异点回答的是"为什么用户选我们"**——拼起来的东西别人也能拼，但「agent 可调 + HamLog 落库 + lumo 可嵌」的组装层和持续运维是我们的。
