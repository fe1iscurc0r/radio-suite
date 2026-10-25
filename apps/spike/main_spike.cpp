// SPDX-License-Identifier: AGPL-3.0-or-later
// WO-274 B 节 spike 入口
#include <QApplication>

#include "engine_spike.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("radio-suite-engine-spike");
  radio::spike::EngineSpike w("127.0.0.1");
  w.setWindowTitle("radio-suite — rigplane engine spike (WO-274)");
  w.resize(1280, 720);
  w.show();
  return app.exec();
}
