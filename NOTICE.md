# NOTICE — radio-suite 依赖与许可

本仓库主许可为 **AGPL-3.0-or-later**（见 [LICENSE](LICENSE) 全文）。

| 依赖 | 版本/钉定 | 许可 | 引入方式 | 说明 |
|---|---|---|---|---|
| Qt 6 | 6.8.3 LTS（本地）/ CI 装最新 6.8.x | LGPL-3.0（部分模块 GPL-3.0） | **动态链接**（DLL/dylib/so 随包分发） | 合规要点见下节；禁止引入 Qt 商业模块 |
| pocketfft | commit `4d865cba`（cpp 分支） | BSD-3-Clause | FetchContent，header-only | FFT；原文 [LICENSES/BSD-3-Clause-pocketfft.txt](LICENSES/BSD-3-Clause-pocketfft.txt) |
| Catch2 | v3.7.1 | BSL-1.0 | FetchContent，仅测试 | 原文 [LICENSES/BSL-1.0.txt](LICENSES/BSL-1.0.txt) |
| RtAudio | commit `06f97b63` | MIT | vendor/（git 元数据已剥离） | 音频输出（WASAPI）；见 [vendor/rtaudio/LICENSE](vendor/rtaudio/LICENSE) |
| kappanhang | master（引入 2026-10-25） | MIT（GitHub NOASSERTION 系识别失败，LICENSE 实查） | **逐文件移植**：apps/desktop/core/audio_stream.* | RS-BA1 音频通路（TX 打包/RX 重排）；头带 Ported-from 标注；© 2020 HA2NON/ES1AKOS |
| SSTVEncoder2 | master（引入 2026-10-25） | Apache-2.0 | 制式参数移植（sstv.cpp）+ NOTICE 摘录 | [docs/sstvencoder2-notice.txt](docs/sstvencoder2-notice.txt)；规格出处 JL Barber "Dayton Paper" |
| rsba1-core（vendor） | 快照随仓 | MIT | **仅作行为参考与 fixtures 来源，不参与构建** | Python 参考实现，见 `libs/protocol/rsba1-core/LICENSE`；协议对齐基准 |
| GLM | （M2 引入） | MIT | — | 仅矩阵/向量数学，不作框架 |

第三方许可原文目录：[LICENSES/](LICENSES/)（含 [LGPL-3.0-only.txt](LICENSES/LGPL-3.0-only.txt)）。

## Qt / LGPLv3 合规边界

- 默认且目前唯一的链接方式是**动态链接**：Windows 用 `windeployqt`，Linux 用
  `linuxdeploy`/AppImage，macOS 用 `macdeployqt`，随产物分发对应 Qt 运行库与
  LGPL 对应的源码获取途径（Qt 官方源码或指向 qt.io 的书面报价）。
- 若未来为单文件分发改**静态链接** Qt：必须按 LGPLv3 §4 提供可重链接目标文件，
  或按 LGPLv3 §3 将本仓该产物转为 GPLv3 再依 AGPLv3 §13 并入。两条路都要在
  本文件与 LICENSE 补记，**不允许“静态链接还按 LGPL 走”**。

## fixtures 来源声明

`tests/fixtures/*.hex` 由 `tests/fixtures/gen_from_reference.py` 生成，
数据出自 `libs/protocol/rsba1-core`（MIT）与其 IC-705 真机验证（2026-08-18，
见 [docs/真机验证报告.md](docs/真机验证报告.md)）。
