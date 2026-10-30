// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/aprs_panel.h — APRS 工作台（APRS-IS + RF + iGate + 地图/消息）
//
// 数据面全部走 radio::aprs：TNC2 解析/生成、台站聚合、消息 ACK/重发、过滤器、
// iGate 去重与 q 构造、KISS、AFSK。面板只做编排与呈现：
//   · APRS-IS：登录/过滤器/重连退避，收包进 StationStore/MessageStore
//   · RF：KISS（TCP/串口，Dire Wolf/LoRa APRS）或 AFSK（50003 音频链）
//   · iGate：RF→IS 追加 qAR，IS→RF 重写路径并去重
//   · 地图：QPainter 瓦片 + 台站/轨迹；消息：收件箱/发件箱/ACK 状态
//   · 持久化：独立 SQLite（%APPDATA%/radio-suite/aprs.db），与 HamLog 无关
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "aprs/afsk.h"
#include "aprs/aprs.h"
#include "aprs/igate.h"
#include "aprs/store.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTcpSocket;
class QTimer;

namespace radio::aprs {
class AprsDb;
}

namespace radio::app {

class AprsMapWidget;
class AprsMessagesDialog;
class KissLink;
class NetworkAudio;

class AprsPanel : public QWidget {
  Q_OBJECT
 public:
  explicit AprsPanel(QWidget* parent = nullptr);
  ~AprsPanel() override;

  void setLoginCall(const QString& call);  // HamLog settings 的 my_callsign
  void setMyPosition(bool has, double lat, double lon);
  void setNetworkAudio(NetworkAudio* audio);
  // AFSK 发射的 PTT 编排（MainWindow 注入：begin=进 TX，end=回 RX）
  void setTxController(std::function<bool()> begin,
                       std::function<void()> end);

  bool isConnected() const;
  QString loginCall() const { return login_call_; }
  bool rfConnected() const;
  int unread() const { return messages_.unread(); }

  // 50003 RX PCM → AFSK 解调（MainWindow 在 RX tap 里调用）
  void feedRxPcm(std::span<const int16_t> pcm);

 public slots:
  void toggleConnect();
  void sendBeacon();
  void sendMessage();

 signals:
  void statusMessage(const QString& msg);
  void unreadChanged(int unread);

 private:
  void buildUi();
  void onConnected();
  void onReadyRead();
  void onDisconnected();
  void onTick();

  void handleTnc2(const std::string& line, bool from_rf);
  void ingest(aprs::Packet packet, bool from_rf);
  bool transmit(const std::string& tnc2, const aprs::Packet& packet,
                const std::string& rf_path);
  void sendIsRaw(const std::string& line);
  bool sendViaRf(const aprs::Packet& packet, const std::string& rf_path);
  void sendMessageTo(const std::string& to, const std::string& text);
  void sendAckFor(const aprs::Packet& packet, bool from_rf);
  void retryMessages();
  void appendLog(const QString& text);

  void refreshTable();
  void selectStation(const QString& key);
  void openMessages();
  void openRfSettings();
  void connectIs();
  void applyRfSettings();
  void startAfskTx(std::vector<float> audio);
  void pumpAfskTx();
  void abortAfskTx();  // 中止 AFSK 发送并释放 PTT（幂等）
  void saveNow();

  // APRS-IS
  QTcpSocket* sock_ = nullptr;
  QString rx_buf_;
  int server_index_ = 0;
  int backoff_s_ = 0;
  bool want_connected_ = false;
  bool reconnect_pending_ = false;

  // RF
  KissLink* kiss_ = nullptr;
  enum class RfMode { Off, KissTcp, KissSerial, Afsk };
  RfMode rf_mode_ = RfMode::Off;
  QString kiss_host_ = "127.0.0.1";
  quint16 kiss_port_num_ = 8001;
  QString serial_name_;
  int serial_baud_ = 9600;
  aprs::AfskDemodulator afsk_rx_;
  std::vector<int16_t> afsk_tx_pcm_;
  size_t afsk_tx_pos_ = 0;
  QTimer* afsk_timer_ = nullptr;
  NetworkAudio* audio_ = nullptr;
  std::function<bool()> tx_begin_;
  std::function<void()> tx_end_;
  bool tx_active_ = false;

  // iGate
  bool igate_rf_to_is_ = false;
  bool igate_is_to_rf_ = false;
  aprs::DuplicateFilter is_dedup_{30, 2048};

  // 数据
  aprs::StationStore stations_{1500, 120};
  aprs::MessageStore messages_{500, 200};
  std::unique_ptr<aprs::AprsDb> db_;
  QTimer* persist_timer_ = nullptr;
  QString login_call_ = "N0CALL";
  bool has_pos_ = false;
  double my_lat_ = 0.0;
  double my_lon_ = 0.0;
  int total_packets_ = 0;
  int rf_packets_ = 0;

  // UI
  QLabel* status_ = nullptr;
  QLabel* detail_ = nullptr;
  QLineEdit* filter_edit_ = nullptr;
  QComboBox* send_path_ = nullptr;
  QCheckBox* igate_rf_is_ = nullptr;
  QCheckBox* igate_is_rf_ = nullptr;
  QPushButton* connect_btn_ = nullptr;
  QPushButton* rf_btn_ = nullptr;
  QPushButton* msg_btn_ = nullptr;
  QPushButton* beacon_btn_ = nullptr;
  AprsMapWidget* map_ = nullptr;
  QTableWidget* table_ = nullptr;
  AprsMessagesDialog* messages_dialog_ = nullptr;
  QString selected_;
  int last_unread_ = 0;
  int tick_count_ = 0;
  QStringList log_lines_;
};

}  // namespace radio::app
