// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/dtmf_dialog.h — DTMF 小键盘（ITU Q.23 双音，生成 .wav）
// 实时发码（电台 DTMF 记忆/CI-V 命令）M3 对码后接入。
#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;

class DtmfDialog : public QDialog {
  Q_OBJECT
 public:
  explicit DtmfDialog(QWidget* parent = nullptr);

 private slots:
  void onKey(char c);
  void onGenWav();

 private:
  QLineEdit* digits_ = nullptr;
  QLabel* info_ = nullptr;
};
