// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/set_mode_dialog.h"

#include <QByteArray>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <span>

#include "app/session.h"

namespace radio::app {
namespace {

struct MenuPreset {
  const char* name;
  uint8_t group;
  uint8_t item;
  const char* hint;  // 值域/格式提示（来自 IC-705 CI-V Reference Guide）
};

// 地址来源：rigplane rigs/ic705.toml [menu]（指南 A7560-8EX-1 页码见条目注释）
constexpr MenuPreset kPresets[] = {
    {"Quick SPLIT（00 45）", 0x00, 0x45, "0=OFF / 1=ON（p.6 control 0045）"},
    {"REF Adjust（00 89）", 0x00, 0x89, "0000-0255（p.7 control 0089）"},
    {"CI-V Transceive（01 31）", 0x01, 0x31, "0=OFF / 1=ON（p.8 control 0131）"},
    {"USB MOD Level（01 16）", 0x01, 0x16, "0000-0255（p.8 control 0116）"},
    {"LAN MOD Level（01 17）", 0x01, 0x17, "0000-0255（p.8 control 0117）"},
    {"DATA OFF MOD（01 18）", 0x01, 0x18, "枚举（MIC/ACC/USB/LAN）"},
    {"DATA MOD（01 19）", 0x01, 0x19, "枚举（MIC/ACC/USB/LAN）"},
    {"System Date（01 65）", 0x01, 0x65, "yyyymmdd BCD（p.9）"},
    {"System Time（01 66）", 0x01, 0x66, "HHMM BCD（p.9）"},
    {"UTC Offset（01 70）", 0x01, 0x70, "±hhmm BCD 3 字节（p.9/p.20）"},
    {"Dot/Dash Ratio（02 52）", 0x02, 0x52, "0028-0045（p.11 control 0252）"},
    {"NB Depth（03 57）", 0x03, 0x57, "0000-0009（p.13 control 0357）"},
    {"NB Width（03 58）", 0x03, 0x58, "0000-0255（p.13 control 0358）"},
    {"VOX Delay（03 59）", 0x03, 0x59, "0000-0020，0.1s 步进（p.14 control 0359）"},
};

}  // namespace

SetModeDialog::SetModeDialog(RadioSession* session, QWidget* parent)
    : QDialog(parent), session_(session) {
  setWindowTitle("Set Mode — 菜单项");
  setMinimumWidth(460);

  auto* root = new QVBoxLayout(this);
  auto* form = new QFormLayout();

  preset_ = new QComboBox(this);
  preset_->setObjectName("set_preset");
  preset_->addItem("（自定义地址）");
  for (const auto& p : kPresets) preset_->addItem(QString::fromUtf8(p.name));
  form->addRow("常用项", preset_);

  group_ = new QSpinBox(this);
  group_->setObjectName("set_group");
  group_->setRange(0, 255);
  group_->setPrefix("0x");
  group_->setDisplayIntegerBase(16);
  form->addRow("Group", group_);

  item_ = new QSpinBox(this);
  item_->setObjectName("set_item");
  item_->setRange(0, 255);
  item_->setPrefix("0x");
  item_->setDisplayIntegerBase(16);
  form->addRow("Item", item_);

  value_ = new QLineEdit(this);
  value_->setObjectName("set_value");
  value_->setPlaceholderText("十六进制值，如 01 / 20 00；留空=读");
  form->addRow("Value (hex)", value_);
  root->addLayout(form);

  auto* hint = new QLabel("读：0x1A 0x05 <group> <item>；写：追加 value 字节。", this);
  hint->setStyleSheet("color: #9e9e9e;");
  root->addWidget(hint);

  readback_ = new QLabel("—", this);
  readback_->setObjectName("set_readback");
  readback_->setStyleSheet("color: #4dd0e1;");
  root->addWidget(readback_);

  auto* buttons = new QDialogButtonBox(this);
  auto* read = buttons->addButton("读取", QDialogButtonBox::ActionRole);
  auto* write = buttons->addButton("写入", QDialogButtonBox::ActionRole);
  read->setObjectName("set_read");
  write->setObjectName("set_write");
  buttons->addButton(QDialogButtonBox::Close);
  root->addWidget(buttons);

  connect(preset_, &QComboBox::currentIndexChanged, this,
          &SetModeDialog::onPresetChanged);
  connect(group_, &QSpinBox::valueChanged, this, [this] { last_key_ = 0; });
  connect(item_, &QSpinBox::valueChanged, this, [this] { last_key_ = 0; });
  connect(read, &QPushButton::clicked, this, &SetModeDialog::readCurrent);
  connect(write, &QPushButton::clicked, this, &SetModeDialog::writeCurrent);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(session_, &RadioSession::stateChanged, this, &SetModeDialog::syncFromState);

  onPresetChanged(0);
}

void SetModeDialog::onPresetChanged(int index) {
  if (index <= 0) return;
  const auto& p = kPresets[index - 1];
  group_->setValue(p.group);
  item_->setValue(p.item);
  value_->setToolTip(QString::fromUtf8(p.hint));
  last_key_ = 0;
  readCurrent();
}

void SetModeDialog::readCurrent() {
  const auto group = static_cast<uint8_t>(group_->value());
  const auto item = static_cast<uint8_t>(item_->value());
  last_key_ = static_cast<quint16>((static_cast<quint16>(group) << 8) | item);
  session_->readMenuItem(group, item);
}

void SetModeDialog::writeCurrent() {
  const auto group = static_cast<uint8_t>(group_->value());
  const auto item = static_cast<uint8_t>(item_->value());
  const QString hex = value_->text().simplified().remove(' ');
  QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
  if (bytes.isEmpty()) {
    readCurrent();
    return;
  }
  last_key_ = static_cast<quint16>((static_cast<quint16>(group) << 8) | item);
  session_->writeMenuItem(
      group, item,
      std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.constData()),
                               static_cast<size_t>(bytes.size())));
}

void SetModeDialog::syncFromState() {
  if (last_key_ == 0) return;
  const auto s = session_->store().snapshot();
  const auto it = s.menu_values.find(last_key_);
  if (it == s.menu_values.end()) {
    readback_->setText("回读：该地址尚未读取（或电台未应答）");
    return;
  }
  QStringList parts;
  for (uint8_t b : it->second) parts << QString("%1").arg(b, 2, 16, QChar('0'));
  readback_->setText(QString("回读：%1 字节 · %2")
                         .arg(it->second.size())
                         .arg(parts.join(' ').toUpper()));
}

}  // namespace radio::app
