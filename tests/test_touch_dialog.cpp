// SPDX-License-Identifier: AGPL-3.0-or-later
// 触摸层：fitDialog 把根布局搬进 QScrollArea 后，原布局里的子布局（QFormLayout、
// QHBoxLayout 行）也必须跟着搬家。否则表单标签/输入框仍按对话框坐标绘制，真机上
// 会出现「表单跑到按钮下面、选项点不到」——2026-11-01 手机截图里就是这么坏的。
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QPaintEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QThread>
#include <QVBoxLayout>

#include <vector>

#include "app/touch_dialog.h"

namespace {

QApplication& app() {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  // Qt 把这两个开关读成函数里的 static，一旦有测试先滚动过就来不及了；
  // 放在建 QApplication 之前，和 main.cpp 的顺序保持一致
  radio::app::touch::installScrollRepaintWorkaround();
  static int argc = 1;
  static char name[] = "radio-ui-tests";
  static char* argv[] = {name, nullptr};
  static QApplication instance(argc, argv);
  return instance;
}

QLabel* labelWithText(QWidget* root, const QString& text) {
  for (auto* l : root->findChildren<QLabel*>())
    if (l->text() == text) return l;
  return nullptr;
}

QPushButton* buttonWithText(QWidget* root, const QString& text) {
  for (auto* b : root->findChildren<QPushButton*>())
    if (b->text() == text) return b;
  return nullptr;
}

}  // namespace

TEST_CASE("fitDialog reparents form rows into scroll content", "[touch][ui]") {
  app();
  qputenv("RADIO_SUITE_TOUCH_FIT", "1");

  QDialog dlg;
  auto* root = new QVBoxLayout(&dlg);
  auto* form = new QFormLayout();
  auto* combo = new QComboBox(&dlg);
  form->addRow("麦克风（PTT 上行）", combo);
  root->addLayout(form);
  auto* group = new QGroupBox("链路质量", &dlg);
  root->addWidget(group);
  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  root->addWidget(buttons);

  radio::app::touch::fitDialog(&dlg);

  auto* scroll = dlg.findChild<QScrollArea*>();
  REQUIRE(scroll != nullptr);
  QWidget* content = scroll->widget();
  REQUIRE(content != nullptr);

  // 子布局里的控件必须跟到滚动页；留在对话框上的话坐标是对话框坐标系
  REQUIRE(content->isAncestorOf(combo));
  REQUIRE(content->isAncestorOf(labelWithText(&dlg, "麦克风（PTT 上行）")));

  content->layout()->activate();
  const int form_y = combo->mapTo(content, QPoint(0, 0)).y();
  const int group_y = group->mapTo(content, QPoint(0, 0)).y();
  const int buttons_y = buttons->mapTo(content, QPoint(0, 0)).y();
  CHECK(form_y < group_y);
  CHECK(group_y < buttons_y);

  // 底部常驻「关闭」，且滚动区在根布局里吃得下剩余高度
  dlg.show();
  QApplication::processEvents();
  auto* close = buttonWithText(&dlg, "关闭");
  REQUIRE(close != nullptr);
  CHECK(close->minimumHeight() >= 44);

  // 常驻按钮必须真被摆到对话框里（手机上一度是 [0,0][0,0]：
  // 控件存在但布局没给它几何，底部看起来就没有关闭键）
  const QPoint close_pos = close->mapTo(&dlg, QPoint(0, 0));
  CHECK(close->isVisible());
  CHECK(close->width() > 0);
  CHECK(close_pos.y() >= 0);
  CHECK(close_pos.y() + close->height() <= dlg.height());
  CHECK(close_pos.x() + close->width() <= dlg.width());
}

TEST_CASE("fitDialog clamps a minimum height taller than the screen",
          "[touch][ui]") {
  app();
  qputenv("RADIO_SUITE_TOUCH_FIT", "1");

  QDialog dlg;
  // SSTV 对话框就是这种：setMinimumSize(440, 480)，手机可用高度只有三百多 dp；
  // 最小值压不下去时 resize() 无效，底部常驻「关闭」被顶到屏外
  dlg.setMinimumSize(440, 5000);
  auto* root = new QVBoxLayout(&dlg);
  root->addWidget(new QLabel("tall content", &dlg));

  radio::app::touch::fitDialog(&dlg);
  dlg.show();
  QApplication::processEvents();

  const QScreen* screen =
      dlg.screen() ? dlg.screen() : QGuiApplication::primaryScreen();
  const QRect avail = screen->availableGeometry();
  CHECK(dlg.height() <= avail.height());

  auto* close = buttonWithText(&dlg, "关闭");
  REQUIRE(close != nullptr);
  const QPoint pos = close->mapTo(&dlg, QPoint(0, 0));
  CHECK(close->isVisible());
  CHECK(pos.y() >= 0);
  CHECK(pos.y() + close->height() <= dlg.height());
}

namespace {

// 滚动时 Qt 的「快移动」只搬像素、只补画新露出来的窄条：中间那些行前后都
// 完整可见，不强制整块重绘就一次都不会重画。用会自数的行当探针。
class RepaintSpy : public QLabel {
 public:
  using QLabel::QLabel;
  int paints = 0;

 protected:
  void paintEvent(QPaintEvent* ev) override {
    ++paints;
    QLabel::paintEvent(ev);
  }
};

void spin(int ms = 200) {
  for (int i = 0; i < ms / 10; ++i) {
    QApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

}  // namespace

TEST_CASE("scrolling repaints the rows that never left the screen",
          "[touch][ui]") {
  // 开关在 app() 里装（建 QApplication 之前）。不装的话下面那条 CHECK
  // 会挂（实测 mid paints=0），手机上就是屏幕左端慢一排的滞后带
  app();
  qputenv("RADIO_SUITE_TOUCH_FIT", "1");

  QWidget host;
  auto* content = new QWidget();
  auto* lay = new QVBoxLayout(content);
  std::vector<RepaintSpy*> rows;
  for (int i = 0; i < 40; ++i) {
    auto* spy = new RepaintSpy(QString("row %1").arg(i));
    lay->addWidget(spy);
    rows.push_back(spy);
  }
  auto* scroll = new QScrollArea(&host);
  scroll->setWidgetResizable(true);
  scroll->setWidget(content);
  host.resize(240, 120);
  scroll->setGeometry(0, 0, 240, 120);
  host.show();
  spin();

  RepaintSpy* mid = nullptr;
  for (auto* spy : rows) {
    // 滚动前后都完整落在视口里的那一行：它没有任何「必须重画」的理由，
    // 正是快移动只会搬像素、不会重画的那批
    const QPoint top = spy->mapTo(scroll->viewport(), QPoint(0, 0));
    const QPoint moved(top.x(), top.y() - 30);
    const QRect vp = scroll->viewport()->rect();
    if (vp.contains(top) && vp.contains(moved)) {
      mid = spy;
      break;
    }
  }
  REQUIRE(mid != nullptr);
  REQUIRE(mid->paints > 0);  // 先确认探针真的在画，否则下面的断言是空的
  mid->paints = 0;

  REQUIRE(scroll->verticalScrollBar()->maximum() > 0);
  scroll->verticalScrollBar()->setValue(30);
  spin();

  // 没滚出画面的行也必须重画一遍，否则真机上它就是上一帧留下的残影
  CHECK(mid->paints > 0);
}
