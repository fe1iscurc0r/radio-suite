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
   ——sanitizer 改变退出序，这类问题 sanitizer 看不见，**量具盲区**。
2. **对症修复已验证**：`tests/qt_test_app.h` 的静态存储期 QCoreApplication 改为
   堆分配、有意泄漏、永不析构——退出期不再有 app 析构参与，顺序冲突整体消失。
   修复后 plain 构建 **10/10 不崩**，全量 **188/188**（Linux + Qt 6.10.1）。
   代价 = 进程退出跳过 app 析构，测试进程可接受；产品代码不适用本模式。
3. #106 的另一半（bus 析构 double-free）本轮未触发（gui=OFF 下 bus 测试全过）。
4. 回归判据 = 本目录 10 连跑矩阵 + gdb 栈；中间方案 qa_main.cpp 已撤销（与
   Catch2WithMain 冲突，CI 抓到）。

## 采集命令
见 `wsl-*.sh`（构建/探测/取证脚本模板），全部可重跑；现场读数（gdb 栈、矩阵数字）
已冻结在本目录日志文件里。
