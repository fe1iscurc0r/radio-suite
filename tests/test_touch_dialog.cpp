// SPDX-License-Identifier: AGPL-3.0-or-later
// 触摸层：fitDialog 把根布局搬进 QScrollArea 后，原布局里的子布局（QFormLayout、
// QHBoxLayout 行）也必须跟着搬家。否则表单标签/输入框仍按对话框坐标绘制，真机上
// 会出现「表单跑到按钮下面、选项点不到」——2026-11-01 手机截图里就是这么坏的。
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

#include "app/touch_dialog.h"

namespace {

QApplication& app() {
  qputenv("QT_QPA_PLATFORM", "offscreen");
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
