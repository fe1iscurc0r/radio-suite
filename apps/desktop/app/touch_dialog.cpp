// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/touch_dialog.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QScroller>
#include <QVBoxLayout>
#include <QWidget>

namespace radio::app::touch {

namespace {

// 桌面默认保持原样；离屏回归/桌面调试用该环境变量显式打开，手机上是恒开
bool touchEnabled() {
#ifdef Q_OS_ANDROID
  return true;
#else
  return qEnvironmentVariableIsSet("RADIO_SUITE_TOUCH_FIT");
#endif
}

// 手机上没有鼠标，按钮/输入控件触不到 44dp 就是误触/点不中
void bumpHeight(QWidget* root, int min_h) {
  for (auto* w : root->findChildren<QWidget*>()) {
    const bool interactive = qobject_cast<QPushButton*>(w) ||
                             qobject_cast<QComboBox*>(w) ||
                             qobject_cast<QAbstractSpinBox*>(w) ||
                             qobject_cast<QLineEdit*>(w) ||
                             qobject_cast<QCheckBox*>(w) ||
                             qobject_cast<QRadioButton*>(w);
    if (interactive && w->minimumHeight() < min_h) w->setMinimumHeight(min_h);
  }
}

void restyleButtons(QWidget* root) { bumpHeight(root, 44); }

}  // namespace

void fitDialog(QDialog* dlg) {
  if (!touchEnabled()) return;
  if (!dlg || dlg->property("rs_touch_fitted").toBool()) return;
  QLayout* root = dlg->layout();
  if (!root) return;
  const bool box_root = qobject_cast<QBoxLayout*>(root) != nullptr;
  if (!box_root && !qobject_cast<QFormLayout*>(root)) return;
  dlg->setProperty("rs_touch_fitted", true);

  // 原内容整体搬进滚动页：先摘下所有 item 再重新安置（子布局由 addLayout 收养）
  QList<QLayoutItem*> items;
  while (QLayoutItem* it = root->takeAt(0)) items.append(it);
  auto* content = new QWidget(dlg);
  auto* page = new QVBoxLayout(content);
  page->setContentsMargins(0, 0, 0, 0);
  for (QLayoutItem* it : items) {
    if (QWidget* w = it->widget()) {
      w->setParent(content);
      page->addWidget(w);
      delete it;
    } else if (QLayout* l = it->layout()) {
      // 子布局就是 takeAt 返回的 item 本身，addLayout 已收养它并把里面的
      // 控件改父到 content；再 delete 会让 page 持悬空指针（真机上是布局
      // 错位，回归测试直接 0xC0000409）
      page->addLayout(l);
    } else {
      page->addItem(it);  // 弹簧：item 本身就是所有者，直接收养
    }
  }
  page->addStretch(1);

  auto* scroll = new QScrollArea(dlg);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(content);
#ifdef Q_OS_ANDROID
  // QScrollArea 默认只认滚轮/滚动条，手指拖动不会滚；QScroller 把左键拖动
  // 变成惯性滚动（安卓上触摸会被合成成鼠标事件，所以选 LeftMouseButton）
  QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
#endif

  auto* footer = new QWidget(dlg);
  auto* bar = new QHBoxLayout(footer);
  bar->setContentsMargins(0, 0, 0, 0);
  bar->addStretch(1);
  auto* close = new QPushButton("关闭", footer);
  close->setMinimumSize(110, 48);
  bar->addWidget(close);
  QDialog* self = dlg;
  QObject::connect(close, &QPushButton::clicked, self, &QDialog::reject);

  if (auto* form = qobject_cast<QFormLayout*>(root)) {
    form->addRow(scroll);
    form->addRow(footer);
  } else {
    auto* box = static_cast<QBoxLayout*>(root);
    box->addWidget(scroll, 1);
    box->addWidget(footer);
  }

  restyleButtons(content);

  const QScreen* screen =
      dlg->screen() ? dlg->screen() : QGuiApplication::primaryScreen();
  const QRect avail = screen->availableGeometry();
  if (dlg->minimumWidth() > avail.width())
    dlg->setMinimumWidth(qMax(0, avail.width() - 8));
  // 有的对话框自带 minimumHeight（SSTV 是 480dp），比手机可用高度还高；
  // 不压下去 resize() 会被最小值顶住，底部常驻「关闭」就落到屏外了
  if (dlg->minimumHeight() > avail.height())
    dlg->setMinimumHeight(qMax(0, avail.height() - 8));
  dlg->setMaximumSize(avail.width(), avail.height());
  // 高度吃满可用区：手机上滚动内容比压缩按钮可靠
  dlg->resize(qMin(qMax(360, dlg->minimumSizeHint().width()), avail.width()),
              avail.height());
}

QWidget* addCloseBar(QWidget* panel, const QString& title) {
#ifdef Q_OS_ANDROID
  if (!panel) return nullptr;
  auto* box = qobject_cast<QVBoxLayout*>(panel->layout());
  if (!box) return nullptr;
  auto* bar = new QWidget(panel);
  bar->setObjectName("rs_panel_close_bar");
  auto* lay = new QHBoxLayout(bar);
  lay->setContentsMargins(6, 2, 6, 2);
  lay->setSpacing(8);
  auto* label = new QLabel(title, bar);
  label->setStyleSheet("color: #cfd6e0; font-size: 11pt;");
  lay->addWidget(label, 1);
  auto* close = new QPushButton("关闭 ✕", bar);
  close->setMinimumSize(96, 44);
  lay->addWidget(close);
  QObject::connect(close, &QPushButton::clicked, panel, &QWidget::hide);
  box->insertWidget(0, bar);
  return close;
#else
  (void)panel;
  (void)title;
  return nullptr;
#endif
}

void showInfo(QWidget* parent, const QString& title, const QString& text) {
  QDialog dlg(parent);
  dlg.setWindowTitle(title);
  auto* root = new QVBoxLayout(&dlg);
  auto* label = new QLabel(text, &dlg);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  auto* scroll = new QScrollArea(&dlg);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(label);
#ifdef Q_OS_ANDROID
  QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
#endif
  root->addWidget(scroll, 1);
  auto* row = new QHBoxLayout();
  row->addStretch(1);
  auto* close = new QPushButton("关闭", &dlg);
  close->setMinimumSize(110, 44);
  row->addWidget(close);
  root->addLayout(row);
  QObject::connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
#ifdef Q_OS_ANDROID
  if (const QScreen* screen = dlg.screen() ? dlg.screen()
                                           : QGuiApplication::primaryScreen())
    dlg.resize(screen->availableGeometry().size());
#else
  dlg.resize(560, 380);
#endif
  dlg.exec();
}

}  // namespace radio::app::touch