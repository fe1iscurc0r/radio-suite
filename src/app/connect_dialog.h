// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/connect_dialog.h — 连接设置（原厂 Connect Setting 对应件）
// 第一片：表单生成启动命令并复制（会话热切换 M3；重启即用真机）
#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QLabel;

class ConnectDialog : public QDialog {
  Q_OBJECT
 public:
  explicit ConnectDialog(QWidget* parent = nullptr);

 private slots:
  void onCopy();

 private:
  QComboBox* mode_ = nullptr;
  QLineEdit* host_ = nullptr;
  QLineEdit* user_ = nullptr;
  QLineEdit* pass_ = nullptr;
  QLabel* cmdline_ = nullptr;
};
