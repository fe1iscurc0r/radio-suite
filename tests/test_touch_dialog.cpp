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
#include <QLabel>
#include <QPushButton>
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
  auto* close = buttonWithText(&dlg, "关闭");
  REQUIRE(close != nullptr);
  CHECK(close->minimumHeight() >= 44);
}
