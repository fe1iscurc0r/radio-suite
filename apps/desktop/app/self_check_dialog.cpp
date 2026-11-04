// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/self_check_dialog.h"

#include "app/touch_dialog.h"

#include <QApplication>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPermissions>
#include <QPushButton>
#include <QVBoxLayout>

namespace radio::app {
namespace {

QLabel* cell(const QString& text, bool bold = false) {
  auto* label = new QLabel(text);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  if (bold) {
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
  }
  return label;
}

}  // namespace

SelfCheckDialog::SelfCheckDialog(std::function<ReportInput()> provider,
                                 QWidget* parent)
    : QDialog(parent), provider_(std::move(provider)) {
  setWindowTitle("自检台账 · 链路与权限");
  setObjectName("self_check_dialog");
  setMinimumWidth(560);

  auto* root = new QVBoxLayout(this);

  auto* actions = new QHBoxLayout();
  auto* refresh_btn = new QPushButton("刷新", this);
  connect(refresh_btn, &QPushButton::clicked, this, &SelfCheckDialog::refresh);
  actions->addWidget(refresh_btn);
#ifdef Q_OS_ANDROID
  // 只有 Android 上「申请」才有意义：桌面没有运行时权限这一层。
  {
    auto* ask_btn = new QPushButton("申请麦克风", this);
    ask_btn->setObjectName("rs_ask_mic");
    connect(ask_btn, &QPushButton::clicked, this, [this] {
      QMicrophonePermission mic;
      qApp->requestPermission(mic, [this](const QPermission& perm) {
        if (perm.status() == Qt::PermissionStatus::Granted) {
          refresh();
          return;
        }
        touch::showInfo(
            this, "麦克风未授予",
            "系统里没给权限，应用内不能再弹第二次。去「设置 → 应用 → radio-suite "
            "→ 权限」里打开，再回本页按「刷新」。");
      });
    });
    actions->addWidget(ask_btn);
  }
#endif
  actions->addStretch(1);
  root->addLayout(actions);

  grid_ = new QGridLayout();
  grid_->setHorizontalSpacing(12);
  grid_->setColumnStretch(3, 1);  // 表头由 render() 统一建，这里不留第二份
  root->addLayout(grid_, 1);

  refresh();
  radio::app::touch::fitDialog(this);
}

QString SelfCheckDialog::noteAt(int row) const {
  if (row < 0 || row >= rows_.size()) return QString();
  return rows_[row].note;
}

void SelfCheckDialog::refresh() {
  last_input_ = provider_ ? provider_() : ReportInput{};
  fillAndroidFacts(last_input_);  // 非 Android 是空操作
  render();
}

void SelfCheckDialog::render() {
  // 清空上一次的内容（表头在 0 行，数据从 1 行起）
  while (QLayoutItem* it = grid_->takeAt(0)) {
    if (QWidget* w = it->widget()) w->deleteLater();
    delete it;
  }
  rows_ = buildDeviceReport(last_input_);

  grid_->addWidget(cell("分组", true), 0, 0);
  grid_->addWidget(cell("项", true), 0, 1);
  grid_->addWidget(cell("状态", true), 0, 2);
  grid_->addWidget(cell("说明", true), 0, 3);

  for (int i = 0; i < rows_.size(); ++i) {
    const ReportRow& r = rows_[i];
    grid_->addWidget(cell(r.group), i + 1, 0);
    grid_->addWidget(cell(r.item, true), i + 1, 1);
    grid_->addWidget(cell(probeStateText(r.state)), i + 1, 2);
    grid_->addWidget(cell(r.note), i + 1, 3);
  }
}

}  // namespace radio::app
