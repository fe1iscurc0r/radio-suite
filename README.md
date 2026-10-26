# radio-suite

独立业余电台控制台：CI-V 电台控制（IC-705 等 Icom）、OpenGL 频谱瀑布、PTT 安全门禁、
QSO 日志。**独立 AGPL-3.0 项目，可被 [scratchpad](https://github.com/fe1iscurc0r/scratchpad)
以本机总线方式集成**——两边同许可、代码可流动，但集成默认走 `--headless --bus` 总线，
不共享状态机。

![status](https://img.shields.io/badge/milestone-M0%20spike-blue) ![license](https://img.shields.io/badge/license-AGPL--3.0--or--later-blue)

## 形态

| 形态 | 用法 | 说明 |
|---|---|---|
| 独立应用 | `radio-suite`（Qt Widgets 原生窗口） | 调谐 / 频谱 / PTT / 日志 |
| 可嵌入 | `radio-suite --headless --bus 8765`（**M5 第一片**） | 本机只读 HTTP 总线，宿主只作消费者；无 WebSocket |

## 构建

依赖：CMake ≥ 3.21、C++20 编译器；GUI 需要 Qt 6.4+（推荐 6.8 LTS）。
pocketfft 与 Catch2 由 FetchContent 拉取（**首次 configure 需要网络**，版本钉死见
[NOTICE.md](NOTICE.md)）。

```bash
# Windows（MSVC + Qt）：Qt 路径不要写进仓库，configure 时给前缀即可
cmake -S . -B build -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
./build/apps/desktop/app/radio-suite --mock

# Linux（headless：不编 Qt Widgets，但 UDP/CI-V/测试仍需 Qt6，音频需 ALSA）
sudo apt install -y cmake ninja-build g++ qt6-base-dev libasound2-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=OFF
cmake --build build -j
ctest --test-dir build --output-on-failure
```

离线构建：设置 `FETCHCONTENT_SOURCE_DIR_POCKETFFT` / `FETCHCONTENT_SOURCE_DIR_CATCH2`
指向本地源码目录。

## 模块

| 目录 | target | 里程碑 | 内容 |
|---|---|---|---|
| `apps/desktop/core` | `radio_core` | M0/M1 | CI-V 编解码、频段白名单、PTT 门禁、状态单源、transport：mock + **UDP 真机链（RS-BA1 V2，`--radio`）** |
| `libs/dsp` | `radio_dsp` | M0/M2 | pocketfft FFT、EMA/峰值保持/噪声底、合成信号 |
| `apps/desktop/app` | `radio-suite` | M0/M3 | Qt 主窗口、QOpenGLWidget 瀑布 |
| `apps/desktop/audio` | — | M2 | RtAudio 输入 |
| `apps/desktop/logbook` | `radio_logbook` | M4 | HamLog 共库、QtSql CRUD/搜索、ADIF、LoTW 上传 |
| `apps/desktop/bus` | — | M5 | `--headless --bus` 只读 HTTP + rigctld TCP |

## 协议事实基准

CI-V 行为以 [`libs/protocol/rsba1-core`](libs/protocol/rsba1-core)（MIT **活子项目**，合作工作流见 [docs/rsba1-core-collab.md](docs/rsba1-core-collab.md)）与
[docs/真机验证报告.md](docs/真机验证报告.md)（IC-705 端到端实测 2026-08-18）为准；
`tests/fixtures/` 由 `tests/fixtures/gen_from_reference.py` 从参考实现生成，
C++ 用例与之逐字节对照。参考实现只作行为基准与 fixtures 来源，**不参与构建、不进
运行时**。

## 安全模型

- PTT 二次确认（5 s 窗口）+ 发射 10 分钟自动回 RX，判定在 `core/safety.cpp`，
  UI 只呈现；
- 设频唯一出口走 `core/bands.cpp` 中国业余频段白名单，越界命令不出仓；
- mock 电台同样执行白名单（带外 NAK），保证 CI 能测到门禁语义。

## 许可

AGPL-3.0-or-later（全文见 [LICENSE](LICENSE)）。第三方依赖与 Qt/LGPL 动态链接
合规边界见 [NOTICE.md](NOTICE.md)，许可原文在 [LICENSES/](LICENSES/)。
