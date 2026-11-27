# 2026-11-27 · Linux + Qt 6.10.1：bus 退出期段错误定因 + 链路活性时钟修复

## 目的

收掉 #106 的另一半：#106 只把 SQLite 用例的退出期段错误修掉了，
「bus 析构 double-free」当时记为「gui=OFF 下未复现」。本轮先纠正量具，再做对照。

## 量具纠正：上一轮其实是空转

上一轮 WSL 的 Qt 6.10.1 套件**没有 QtWebSockets**，而 `Qt6HttpServer` 依赖它；
`apps/desktop/bus/CMakeLists.txt` 拿不到 `Qt6::HttpServer` 就整个目录跳过。
所以「gui=OFF 下 bus 测试全过」= bus 根本没编、没跑——是量具盲区，不是结论。

本轮补装：`aqt install-qt linux desktop 6.10.1 linux_gcc_64 -m qtwebsockets`，
`radio_bus_tests` 目标才出现（144 个编译单元）。

## 对照实验（决定性）

环境：WSL Ubuntu，Qt 6.10.1 linux_gcc_64，Debug，`QT_QPA_PLATFORM=offscreen`，
`BUILD_GUI=ON`、`BUILD_TESTS=ON`，Catch2/pocketfft 走本地源码目录（离线）。

| 构建 | 结果 |
|---|---|
| **变异**：`tests/qt_test_app.h` 改回静态存储期 `QCoreApplication` | **20/20 退出期 SIGSEGV**（调用栈与 #106 同族：`__run_exit_handlers` 期静态析构） |
| **修复**（现状 main）：堆分配、有意泄漏、永不析构 | 跑 50 轮出现 1 次**与本项无关的时序 flake**（见下），修掉后 **30/30 干净** |

复现命令（本轮实跑）：

```bash
wsl -e bash -lc "cd /root/rs && cmake --build build-gui --target radio_bus_tests -j$(nproc)"
wsl -e bash -lc "cd /root/rs && QT_QPA_PLATFORM=offscreen ./build-gui/tests/radio_bus_tests"
```

变异样本日志：`mutation-crash.log`（25 个用例全过、进程退出期崩）。
修复后的轮次摘要：`fixed-runs.txt`。

## 顺带抓到并修掉的真缺陷：链路活性用墙钟

排查上面的偶发 flake 时定位到 `test_bus_link_health.cpp:186`（「判死之后
`link_reply_age_ms` 必须大于 `link_timeout_ms`」偶发不成立）。根因不是测试：
`RadioSession` 的活性判定用 `QDateTime::currentMSecsSinceEpoch()`——**墙上时钟**。
本机实测到一次会话里日期跨了几天（VM 挂起/恢复 + NTP 校正），墙钟往回拨时，
「tick 判死用的 now」与「请求时现算 age 的 now」会自相矛盾。

修法：活性链路改用 `steady_clock` 单调时钟（`session.cpp` 内 `monoMs()`，+1 避开
`last_rx_ms_==0` 的「从未收到」哨兵）；PTT 门禁仍用墙钟（对外可见的剩余秒数按墙钟走）。
修复后 `radio_bus_tests` 连跑 **30/30**，之前的偶发失败不再出现。

## 证据清单

- `fixed-runs.txt`：修复构建的轮次摘要（含变异前的 9/10 与变异 rc=139）。
- `mutation-crash.log`：静态析构变异的崩溃样本（用例全过后退出期崩）。
- 本 NOTES：量具纠正、对照矩阵、复现命令、时钟缺陷与修法。

## 结论

- #106 的 bus 那一半**不是「未复现」，是同一根因的另一个宿主**：静态度器在退出期
  与 Qt 静态清理撞序；堆分配修法对 bus/SQLite 两边都成立（变异对照 20/20 vs 0/20）。
- 旧记录「gui=OFF 下 bus 全过」按量具盲区处理，不再作为结论引用。