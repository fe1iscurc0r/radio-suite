// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/touch_dialog.h"

#include <QAbstractItemView>
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
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

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

// 滚动时 Qt 的「快移动」把内容子控件整体挪位置，视口只搬像素、只补画新露
// 出来的一条窄边——留在画面里的行一次都不会重画（离屏用例里可量化：不关
// 快移动，滚动一步后仍在画面中的行 paints=0）。真机甩动台账时这条快路径留
// 下屏幕左端约 1/16 宽的一条，比右边慢一排，中间一道竖直缝（x≈148）。
void installScrollRepaintWorkaround() {
  // Qt 只在第一次用到时读这两个变量，所以必须在建任何控件之前调
  qputenv("QT_NO_FAST_MOVE", "1");
  qputenv("QT_NO_FAST_SCROLL", "1");
}

#ifdef Q_OS_ANDROID
namespace {

// Qt 6.7 安卓上第二个顶层窗口（触摸对话框）经常把上一帧留在屏幕上：点一下
// 按钮，控件状态和新帧都对了，画面却还是旧的，要再点一下才刷新（真机实测：
// 应用频差后 TX 标签迟一步，下拉选完也常这样）。抬手后隔一拍再补一次整窗
// 重绘，第二帧才真正合成上屏。只装在触摸对话框上，桌面不受影响。
class TapRepaint : public QObject {
 public:
  explicit TapRepaint(QWidget* w) : QObject(w), w_(w) {}

  bool eventFilter(QObject* obj, QEvent* ev) override {
    // 控件自己的过滤器收不到子控件的输入事件（都被窗口派发给命中的子控件），
    // show 之后再挂到顶层窗口上，才看得到整个对话框里的每次抬手
    if (obj == w_ && ev->type() == QEvent::Show) {
      if (QWindow* wh = w_->windowHandle()) wh->installEventFilter(this);
      return false;
    }
    if (ev->type() == QEvent::MouseButtonRelease) {
      QTimer::singleShot(60, w_, [w = w_] {
        w->update();
        if (QWindow* wh = w->windowHandle()) wh->requestUpdate();
      });
    }
    return false;
  }

 private:
  QWidget* w_;
};
}  // namespace
#endif

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
  // 手指拖动是二维的，横向会跟着乱窜把左侧标签挤出屏幕；只保纵向。
  // 光藏滚动条不够：内容最小宽度常比视口多几十 dp，横向范围还在，手指带点
  // 横向抖动整页就被拖偏且不会自己回去。直接把这个范围钉死为 0。
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* hbar = scroll->horizontalScrollBar();
  QObject::connect(hbar, &QScrollBar::rangeChanged, hbar,
                   [hbar](int, int) { hbar->setRange(0, 0); });
  hbar->setRange(0, 0);
  // 甩动结束时橡皮筋回弹的那一帧也常丢：手指带出的横向偏移留在屏上，要再点
  // 一下才回正（真机：SSTV/中继对话框滚完左侧标签被切掉）。回到 Inactive 后
  // 补一帧，画面才落在最终位置。
  QObject::connect(QScroller::scroller(scroll->viewport()),
                   &QScroller::stateChanged, scroll,
                   [scroll](QScroller::State state) {
                     if (state != QScroller::Inactive) return;
                     QTimer::singleShot(80, scroll, [scroll] {
                       scroll->viewport()->update();
                       if (QWindow* wh = scroll->window()->windowHandle())
                         wh->requestUpdate();
                     });
                   });
#endif

  auto* footer = new QWidget(dlg);
  auto* bar = new QHBoxLayout(footer);
  bar->setContentsMargins(0, 0, 0, 0);
  bar->addStretch(1);
  auto* close = new QPushButton("关闭", footer);
  close->setObjectName("rs_pinned_close");
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
  // 铺满可用区域，并且**定位在 (0,0)**。Qt 6.7 安卓对多顶层窗口的触摸换算
  // 把窗口位置按 dp/dpr 多减了一次：窗口 x=242dp 时点击坐标整体偏 88dp，
  // 按钮右半永远点不到（真机实测）。位置归零后换算才和画面对得上；
  // 对话框本来也该是整屏 sheet，手机上没有窗口管理器可拖。
  dlg->setGeometry(avail.x(), avail.y(), avail.width(), avail.height());
  // show 时 QDialog 还会按尺寸再居中一次，必须显式钉住位置/尺寸
  dlg->setAttribute(Qt::WA_Moved, true);
  dlg->setAttribute(Qt::WA_Resized, true);
#ifdef Q_OS_ANDROID
  dlg->installEventFilter(new TapRepaint(dlg));
#endif
}

#ifdef Q_OS_ANDROID
namespace {

// 触摸合成出来的鼠标事件在弹窗里走不到 Qt 自己的 itemSelected 分支（真机
// 实测：按下/抬起都进了列表视口，行号也对，弹窗却只是收起）。这里按坐标
// 自己选行：抬起点离按下点太远当作拖动（滚动列表），不选。
class PopupClickFilter : public QObject {
 public:
  PopupClickFilter(QComboBox* combo, QAbstractItemView* view)
      : combo_(combo), view_(view) {}

  bool eventFilter(QObject*, QEvent* ev) override {
    switch (ev->type()) {
      case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(ev);
        press_ = me->position().toPoint();
        moved_ = false;
        break;
      }
      case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(ev);
        if ((me->position().toPoint() - press_).manhattanLength() > 24)
          moved_ = true;
        break;
      }
      case QEvent::MouseButtonRelease: {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() != Qt::LeftButton || moved_) break;
        const QModelIndex idx = view_->indexAt(me->position().toPoint());
        if (!idx.isValid()) break;
        // 不能在输入事件的过滤器里直接 setCurrentIndex/hidePopup：真机上模型
        // 已经改成新选项，屏幕却要等下一次别处点击才刷新。推迟到下一轮事件
        // 循环收尾，并补一次整窗重绘，画面才跟上。
        QTimer::singleShot(0, combo_, [combo = combo_, row = idx.row()] {
          combo->setCurrentIndex(row);
          combo->hidePopup();
          combo->update();
          if (QWidget* top = combo->window()) top->update();
        });
        return true;
      }
      default:
        break;
    }
    return false;
  }

 private:
  QComboBox* combo_;
  QAbstractItemView* view_;
  QPoint press_;
  bool moved_ = false;
};
}  // namespace
#endif

void installPopupOriginFix() {
#ifdef Q_OS_ANDROID
  // Qt 6.7 安卓把顶层窗口的触摸位置按「窗口位置 ÷ dpr」错位，窗口在 (0,0)
  // 才准。下拉弹窗由 Qt 贴在控件旁边，位置必非零，选项永远点不中；每次
  // 弹出后钉到左上角，换算才和画面对得上。弹窗尺寸不动，点外面照常收起。
  class PopupOrigin : public QObject {
   public:
    bool eventFilter(QObject* obj, QEvent* ev) override {
      if (ev->type() == QEvent::Show) {
        auto* w = qobject_cast<QWidget*>(obj);
        if (w && w->isWindow() && w->inherits("QComboBoxPrivateContainer")) {
          w->move(0, 0);
          if (w->windowHandle()) w->windowHandle()->setPosition(QPoint(0, 0));
          // 弹窗的 show 里还会按列表高度调一次几何，等这一轮结束再钉
          QTimer::singleShot(0, w, [w] {
            w->move(0, 0);
            if (w->windowHandle()) w->windowHandle()->setPosition(QPoint(0, 0));
          });
          // 弹窗的选择逻辑自己接管（见 PopupClickFilter）
          if (auto* v = w->findChild<QAbstractItemView*>()) {
            auto* combo = qobject_cast<QComboBox*>(w->parentWidget());
            if (combo && !v->viewport()->property("rs_popup_click").toBool()) {
              v->viewport()->setProperty("rs_popup_click", true);
              v->viewport()->installEventFilter(
                  new PopupClickFilter(combo, v));
            }
          }
        }
      }
      return false;
    }
  };
  static bool installed = false;
  if (installed) return;
  installed = true;
  QCoreApplication::instance()->installEventFilter(new PopupOrigin());
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
                                           : QGuiApplication::primaryScreen()) {
    const QRect avail = screen->availableGeometry();
    // 和 fitDialog 同理：窗口必须落在 (0,0)，否则触摸坐标按窗口位置偏移
    dlg.setGeometry(avail.x(), avail.y(), avail.width(), avail.height());
    dlg.setAttribute(Qt::WA_Moved, true);
    dlg.setAttribute(Qt::WA_Resized, true);
  }
#else
  dlg.resize(560, 380);
#endif
  dlg.exec();
}

}  // namespace radio::app::touch