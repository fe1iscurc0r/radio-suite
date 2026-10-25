// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · logbook/lotw_upload_dialog.h — LoTW 上传（对齐原版 LoTW_Upload_Dialog）
// 流程（原版同款）：①TQSL 命令行签名 ADIF→.tq8 ②HTTP POST .tq8 到
// https://lotw.arrl.org/lotw/upload（multipart，UA 伪装 TQSL）。代理走应用级
// QNetworkProxy（proxy_settings_dialog 设置）。
#pragma once

#include <QDialog>
#include <QNetworkAccessManager>
class QNetworkReply;

class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

namespace radio::logbook {

class HamLogDb;

class LotwUploadDialog : public QDialog {
  Q_OBJECT
 public:
  explicit LotwUploadDialog(HamLogDb& db, QWidget* parent = nullptr);

 private slots:
  void onPickAdif();
  void onUploadFinished(QNetworkReply* reply);
  void onFindTqsl();
  void onUpload();


 private:
  QString findTqsl() const;
  QByteArray signedTq8() const;  // 上次签名产物路径（成员记录）

  HamLogDb& db_;
  QLineEdit* adi_path_ = nullptr;
  QLineEdit* tqsl_path_ = nullptr;
  QPushButton* upload_btn_ = nullptr;
  QTextEdit* log_ = nullptr;
  QNetworkAccessManager nam_;
  QString tq8_path_;
};

}  // namespace radio::logbook
