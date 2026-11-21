// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QCoreApplication>

// Qt 6.10 退出期 QSql 清理段错误（#106）的对症修复：
// 静态存储期的 QCoreApplication 在 __run_exit_handlers 里析构时，
// 析构序撞上 Qt 的插件/驱动静态清理 → libQt6Sql 里段错误
// （gdb 栈见 evidence/2026-11-20-linux-qt610/）。
// 改堆分配、有意泄漏、永不析构：退出期没有 app 对象析构参与，顺序问题整体消失。
// 代价：进程退出跳过 app 析构——测试进程无所谓；产品代码不适用本模式。
inline QCoreApplication& qtTestApp() {
  static int argc = 1;
  static char name[] = "radio-tests";
  static char* argv[] = {name, nullptr};
  static QCoreApplication* app = new QCoreApplication(argc, argv);  // 有意泄漏
  return *app;
}
