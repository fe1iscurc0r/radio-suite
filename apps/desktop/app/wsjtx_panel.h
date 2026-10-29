// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/wsjtx_panel.h — WSJT-X 接入工作台（解码/状态/QSO/控制）
#pragma once

#include <QString>
#include <QWidget>
#include <vector>

#include "wsjtx/wsjtx.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;

namespace radio::app {

class WsjtxClient;

class WsjtxPanel : public QWidget {
  Q_OBJECT
 public:
  explicit WsjtxPanel(QWidget* parent = nullptr);
  ~WsjtxPanel() override;

  WsjtxClient* client() const { return client_; }
  bool isListening() const;
  void setMyGrid(const QString& grid);

  void startListening(
      const QString& group = QString::fromLatin1(radio::wsjtx::kDefaultGroup),
      quint16 port = radio::wsjtx::kDefaultPort);
  void stopListening();

  // 供 UDP 信号与测试直接喂数据
  void applyStatus(const radio::wsjtx::Status& status, const QString& client_id);
  void addDecode(const radio::wsjtx::Decode& decode, const QString& client_id);
  void addQso(const radio::wsjtx::QsoLogged& qso);
  void addWspr(const radio::wsjtx::WsprDecode& decode);
  void setAdif(const QString& adif);

  // 解码表激活（双击/测试共用）：解析呼号与网格，带上绝对频率发给主窗口
  bool activateDecode(int row);

 signals:
  void statusMessage(const QString& message, bool error);
  void decodeActivated(const QString& call, const QString& grid,
                       quint64 frequency_hz, const QString& mode);

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  void buildUi();
  void onReply();
  void onSendFreeText();
  void onSetLocation();
  void onSettings();
  void onClearDecodes();
  void refreshListenButton();
  void flash(const QString& message, bool error = false);

  WsjtxClient* client_ = nullptr;
  QLabel* state_label_ = nullptr;
  QLabel* client_label_ = nullptr;
  QLabel* radio_label_ = nullptr;
  QLabel* tx_label_ = nullptr;
  QLabel* footer_label_ = nullptr;
  QTableWidget* decodes_ = nullptr;
  QTableWidget* qsos_ = nullptr;
  QTableWidget* wsprs_ = nullptr;
  QPlainTextEdit* adif_view_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  QPushButton* listen_btn_ = nullptr;
  QPushButton* reply_btn_ = nullptr;
  QLineEdit* free_text_edit_ = nullptr;
  QLineEdit* grid_edit_ = nullptr;

  std::vector<radio::wsjtx::Decode> decodes_cache_;
  quint64 last_dial_hz_ = 0;
  QString last_client_ = "?";
  bool auto_started_ = false;
};

}  // namespace radio::app
