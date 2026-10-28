// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/aprs_messages_dialog.h — APRS 消息收件箱/发件箱（ACK 状态）
#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace radio::aprs {
class MessageStore;
}

namespace radio::app {

class AprsMessagesDialog : public QDialog {
  Q_OBJECT
 public:
  explicit AprsMessagesDialog(QWidget* parent = nullptr);

  void reload(const radio::aprs::MessageStore& store);
  void setMyCall(const QString& call);
  void setCanSend(bool can_send);

 signals:
  void sendRequested(const QString& to, const QString& text);
  void markAllReadRequested();

 private:
  void onSend();

  radio::aprs::MessageStore* store_ = nullptr;
  QTableWidget* inbox_ = nullptr;
  QTableWidget* outbox_ = nullptr;
  QLabel* summary_ = nullptr;
  QLineEdit* to_edit_ = nullptr;
  QLineEdit* text_edit_ = nullptr;
  QPushButton* send_btn_ = nullptr;
  QString my_call_ = "N0CALL";
};

}  // namespace radio::app
