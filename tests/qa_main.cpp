// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tests/qa_main.cpp — SQL 测试目标的 QCoreApplication 主函数
//
// 为什么要有它：Catch2 默认 main 不建 QCoreApplication，QSql 驱动/插件在进程
// 退出期（__run_exit_handlers）静态析构时顺序错乱，Qt 6.10.1 上实测段错误
// （gdb 栈：QObject::~QObject → destroyed() → libQt6Sql，见 evidence/2026-11-20-linux-qt610/）。
// ASan 下不复现（退出序被改变）——这类「退出期析构序」问题 sanitizer 看不见。
// 给 SQL 测试装上 app 对象 = 让 Qt 的清理走正确顺序。
#include <QCoreApplication>
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  return Catch::Session().run(argc, argv);
}
