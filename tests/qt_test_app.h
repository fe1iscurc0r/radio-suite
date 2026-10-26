// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QCoreApplication>

inline QCoreApplication& qtTestApp() {
  static int argc = 1;
  static char name[] = "radio-tests";
  static char* argv[] = {name, nullptr};
  static QCoreApplication app(argc, argv);
  return app;
}
