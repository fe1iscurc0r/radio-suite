// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tests/qa_main.cpp — SQL 测试目标的 QCoreApplication 主函数
//
// 为什么要有它：Catch2 默认 main 不建 QCoreApplication，QSql 相关对象在进程
// 退出期（__run_exit_handlers）静态析构时打穿 Qt6Sql（gdb 栈：QObject::~QObject
// → destroyed() → libQt6Sql，evidence/2026-11-20-linux-qt610/）。实测结论：
//   · radio_logbook_tests 装上 app 对象后 10/10 不再崩（本修复有效）；
//   · radio_aprs_db_tests 仍崩——残留对象不在此层，疑 Qt 6.10.1 内部退出序
//     问题（ASan 0/6 复现：sanitizer 改变退出序后整层不可见）。已定性并落证，
//     根修在上游/后续单，回归判据 = 同目录 evidence 的 10 连跑矩阵。
#include <QCoreApplication>
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  return Catch::Session().run(argc, argv);
}

#include <QCoreApplication>
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  return Catch::Session().run(argc, argv);
}
