// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/connect_dialog.h — 连接设置（原厂 Connect Setting 对应件）
// 这里不只是生成一行命令文本：保存写入持久化偏好，下次启动由 launch_config 读回来
// 构造 transport。Android 上没有命令行输入口，这条对话框是手机上唯一的链路入口。
#pragma once

#include <QDialog>

#include "app/launch_config.h"

class QComboBox;
class QLineEdit;
class QLabel;

class ConnectDialog : public QDialog {
  Q_OBJECT
 public:
  explicit ConnectDialog(radio::app::LinkPrefs prefs, QWidget* parent = nullptr);

  // accept() 之后的表单内容；reject() 时调用方不该看它
  const radio::app::LinkPrefs& prefs() const { return prefs_; }

 private slots:
  void onCopy();
  void onSave();

 private:
  // 表单 → 偏好（explicit_cli=true：用户刚当面表过态，优先级等同命令行）
  radio::app::LinkPrefs collect() const;
  void syncPreview();

  radio::app::LinkPrefs prefs_;
  QComboBox* mode_ = nullptr;
  QLineEdit* host_ = nullptr;
  QLineEdit* user_ = nullptr;
  QLineEdit* pass_ = nullptr;
  QLineEdit* python_ = nullptr;
  QLabel* cmdline_ = nullptr;
};
