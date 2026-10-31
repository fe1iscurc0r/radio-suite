// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/touch_dialog.h"

#include <QDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace radio::app::touch {

#ifdef Q_OS_ANDROID
namespace {

// 手机上没有鼠标，按钮触不到 44dp 就是误触/点不中
void restyleButtons(QWidget* root) {
  for (auto* b : root->findChildren<QPushButton*>())
    if (b->minimumHeight() < 44) b->setMinimumHeight(44);
}

}  // namespace
#endif

void fitDialog(QDialog* dlg) {
#ifdef Q_OS_ANDROID
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
      page->addLayout(l);
      delete it;
    } else {
      page->addItem(it);  // 弹簧：item 本身就是所有者，直接收养
    }
  }
  page->addStretch(1);

  auto* scroll = new QScrollArea(dlg);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(content);

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
  dlg->setMaximumSize(avail.width(), avail.height());
  // 高度吃满可用区：手机上滚动内容比压缩按钮可靠
  dlg->resize(qMin(qMax(360, dlg->minimumSizeHint().width()), avail.width()),
              avail.height());
#else
  (void)dlg;
#endif
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