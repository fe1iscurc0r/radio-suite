# 2026-11-20 · Linux + Qt 6.10.1（WSL Ubuntu-24.04）· #106 定性取证

## 现场
- WSL Ubuntu-24.04，gcc 13.3，CMake 3.28，Qt 6.10.1（aqt linux_gcc_64 + qthttpserver/qtserialport 模块）
- 源码 = rs-sat275（= main 0627f75 + PR #72 wave 提交），`BUILD_GUI=OFF`，Ninja Debug
- 命令：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_GUI=OFF -DCMAKE_PREFIX_PATH=~/Qt/6.10.1/gcc_64 …`，`QT_QPA_PLATFORM=offscreen ctest`

## 实测矩阵
| 构建 | radio_aprs_db_tests / radio_logbook_tests（SQLite 两用例） | 结果 |
|---|---|---|
| plain Debug | 10 连跑 × 2 用例 | **pass=2 / fail=8**（SEGFAULT，~80%） |
| ASan+UBSan Debug | 6 连跑 × 2 用例 | **pass=6 / fail=0** |
| plain + qa_main（QCoreApplication 主函数） | 10 连跑 × 2 | logbook 10/10 修复；aprs_db 仍 ~80% 崩 |

## 崩溃栈（gdb -batch，6/6 命中）
```
#0-2 libQt6Sql.so.6（驱动/连接拆除）
#3 libQt6Core.so.6
#4 QObject::destroyed(QObject*)
#5 QObject::~QObject()
#6 __run_exit_handlers（进程退出期静态析构）
```

## 定性结论
1. 段错误发生在**进程退出期**，打穿 libQt6Sql；间歇性（~80%），ASan/UBSan 下 0 复现
   ——sanitizer 改变了退出序，这类问题 sanitizer 看不见，**量具盲区**。
2. `qa_main.cpp`（给 SQL 测试目标装 QCoreApplication）**修好 logbook 一半**；
   aprs_db 半边仍在，残留物不在测试/库层可达范围，疑 Qt 6.10.1 内部退出序回归。
3. #106 的另一半（bus 析构 double-free）本轮未触发（gui=OFF 下 bus 测试全过）。
4. 回归判据 = 本目录 10 连跑矩阵 + qa_main.cpp 头注释；根修属上游/后续单。

## 采集命令
见 `wsl-*.sh`（构建/探测/取证脚本模板），全部可重跑；现场读数（gdb 栈、矩阵数字）
已冻结在本目录日志文件里。
