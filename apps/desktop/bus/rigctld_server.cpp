// SPDX-License-Identifier: AGPL-3.0-or-later
// 协议语义与 dump_state 移植自 rigplane-core（MIT）——见 rigctld_server.h 头注。
#include "bus/rigctld_server.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace radio::bus {

RigctldServer::RigctldServer(radio::app::RadioSession& session, QObject* parent)
    : QObject(parent), session_(session) {}

bool RigctldServer::start(const Config& cfg, QString* error) {
  server_ = new QTcpServer(this);
  connect(server_, &QTcpServer::newConnection, this,
          &RigctldServer::onNewConnection);
  if (!server_->listen(QHostAddress(cfg.bind_addr), cfg.port)) {
    if (error) *error = QString("rigctld 监听失败 %1:%2").arg(cfg.bind_addr).arg(cfg.port);
    return false;
  }
  qInfo() << "rigctld: tcp://" + cfg.bind_addr + ":" +
                 QString::number(server_->serverPort()) +
                 "（WSJT-X/fldigi 即插；rigplane 语义移植）";
  return true;
}

void RigctldServer::onNewConnection() {
  while (QTcpSocket* s = server_->nextPendingConnection()) {
    connect(s, &QTcpSocket::readyRead, this, &RigctldServer::onClientReadyRead);
    connect(s, &QTcpSocket::disconnected, this, [this, s] { onClientDisconnected(s); });
  }
}

void RigctldServer::onClientReadyRead() {
  auto* sock = qobject_cast<QTcpSocket*>(sender());
  if (!sock) return;
  QByteArray& buf = rx_buf_[sock];
  buf += sock->readAll();
  int nl = 0;
  while ((nl = buf.indexOf('\n')) >= 0) {
    const QString line = QString::fromUtf8(buf.left(nl)).trimmed();
    buf.remove(0, nl + 1);
    if (line.isEmpty()) continue;
    bool close_conn = false;
    const QByteArray resp = handleLine(line, close_conn);
    if (!resp.isEmpty()) sock->write(resp);
    if (close_conn) {
      sock->disconnectFromHost();
      return;
    }
  }
}

void RigctldServer::onClientDisconnected(QObject* sock) {
  rx_buf_.remove(sock);
  sock->deleteLater();
}

int RigctldServer::hamlibModeBit(const QString& name) {
  // hamlib RIG_MODE_*（rigplane handler.py 头注同表）：
  // AM=0x01 CW=0x02 USB=0x04 LSB=0x08 RTTY=0x10 FM=0x20 WFM=0x40
  const QString n = name.toUpper();
  if (n == "AM") return 0x01;
  if (n == "CW" || n == "CWR") return 0x02;
  if (n == "USB" || n == "DIGU") return 0x04;
  if (n == "LSB" || n == "DIGL") return 0x08;
  if (n == "RTTY") return 0x10;
  if (n == "FM") return 0x20;
  if (n == "WFM") return 0x40;
  return 0;
}

QString RigctldServer::modeNameFromHamlib(const QString& name) {
  // hamlib 模式名 → 本仓 civ::Mode 名（DIGU/DIGL 归 USB/LSB）
  const QString n = name.toUpper();
  if (n == "DIGU") return "USB";
  if (n == "DIGL") return "LSB";
  if (n == "CWR") return "CW";
  return n;
}

QByteArray RigctldServer::handleLine(const QString& line, bool& close_conn) {
  // 行解析（rigplane protocol.parse_line）：长短命令同表，\ 前缀剥除
  const QString token = line.section(' ', 0, 0);
  QString args = line.mid(token.length()).trimmed();
  const QString key = token.startsWith('\\') ? token.mid(1) : token;

  const auto s = session_.store().snapshot();
  const auto ok = [] { return QByteArray("RPRT 0\n"); };
  const auto err = [] { return QByteArray("RPRT -1\n"); };

  // VFO 前缀剥离（rigplane contract：accepts_vfo_arg 命令首参可为 VFOA/VFOB；
  // 本仓单接收机——B/未选 VFO 回主值，满足 rigplane unselected 状态采集）
  static const QStringList kVfoLabels = {"VFOA", "VFOB", "Main", "Sub"};
  QString vfo_arg;
  if (key == "f" || key == "m" || key == "F" || key == "M") {
    const QString first = args.section(' ', 0, 0);
    if (kVfoLabels.contains(first)) {
      vfo_arg = first;
      args = args.mid(first.length()).trimmed();
    }
  }

  if (key == "q" || key == "quit") {
    close_conn = true;
    return {};
  }
  if (key == "chk_vfo") return QByteArray("0\n");  // rigplane：vfo_opt=0

  // GET 频率：回 Hz 整数一行
  if (key == "f" || key == "get_freq")
    return QByteArray::number(static_cast<qulonglong>(s.freq_hz)) + '\n';
  // SET 频率：F <hz>（走白名单/门禁；拒绝时 RPRT -1）
  if (key == "F" || key == "set_freq") {
    bool oknum = false;
    const qulonglong hz = args.toULongLong(&oknum);
    if (!oknum) return err();
    return session_.tuneTo(hz) ? ok() : err();
  }
  // GET 模式：mode\npassband 两行（hamlib 语义）
  if (key == "m" || key == "get_mode") {
    const int pb = s.mode == radio::core::civ::Mode::FM ? 10000 : 2400;
    return QByteArray(radio::core::civ::modeName(s.mode)) + '\n' +
           QByteArray::number(pb) + '\n';
  }
  // SET 模式：M <mode> <passband>
  if (key == "M" || key == "set_mode") {
    const QString mode = args.section(' ', 0, 0);
    if (!hamlibModeBit(mode)) return err();
    const auto m = radio::core::civ::modeFromName(modeNameFromHamlib(mode));
    if (!m) return err();
    session_.setMode(*m);
    return ok();
  }
  if (key == "v" || key == "get_vfo") return QByteArray("VFOA\n");
  if (key == "s" || key == "get_split_vfo") return QByteArray("0\nVFOA\n");
  if (key == "t" || key == "get_ptt")
    return QByteArray::number(s.ptt_on ? 1 : 0) + '\n';
  // split 长命令（rigplane initial state 采集：global.tx_state.split）
  if (key == "get_split_freq") return QByteArray("0\n");
  if (key == "get_split_mode") return QByteArray("0\n");
  if (key == "get_powerstat") return QByteArray("1\n");

  // Func 族（rigplane observation poll：u <FUNC>；本仓映射 0x16 族状态）
  if (key == "u" || key == "get_func") {
    const QString f = args.section(' ', 0, 0).toUpper();
    if (f == "NB") return QByteArray::number(s.nb ? 1 : 0) + '\n';
    if (f == "NR") return QByteArray::number(s.nr ? 1 : 0) + '\n';
    if (f == "FAGC" || f == "ANF") return QByteArray::number(s.notch_auto ? 1 : 0) + '\n';
    if (f == "MON") return QByteArray::number(s.moni ? 1 : 0) + '\n';
    if (f == "VOX") return QByteArray::number(s.vox ? 1 : 0) + '\n';
    return QByteArray("0\n");  // 未支持的 func 一律 0（关）
  }
  if (key == "U" || key == "set_func") return ok();  // mock 接受

  // Level 族（rigplane observation poll 依赖；hamlib `l <LEVEL>` / `L <LEVEL> <val>`）
  // RF/AF 0.0–1.0；STRENGTH dB；ATT/PREAMP dB 整数。值取自本仓状态或合理常量。
  if (key == "l" || key == "get_level") {
    const QString lvl = args.section(' ', 0, 0).toUpper();
    if (lvl == "RF") return QByteArray("0.500000\n");
    if (lvl == "AF") return QByteArray("0.500000\n");
    if (lvl == "STRENGTH") return QByteArray::number(s.smeter_dbm, 'f', 1) + '\n';
    if (lvl == "ATT") return QByteArray::number(s.att_20db ? 20 : 0) + '\n';
    if (lvl == "PREAMP") return QByteArray("0\n");
    if (lvl == "SWR") return QByteArray::number(s.swr, 'f', 2) + '\n';
    if (lvl == "ALC" || lvl == "COMP") return QByteArray("0.000000\n");
    return err();
  }
  if (key == "L" || key == "set_level") return ok();  // 接受并忽略（mock）

  // 长命令：\chk_vfo / \dump_state（WSJT-X 建连探测）
  if (key == "chk_vfo") return QByteArray("0\n");
  if (key == "dump_state") return dumpState();

  // 未实现命令：按 hamlib 语义回参数错误（WSJT-X 会跳过）
  return err();
}

QByteArray RigctldServer::dumpState() const {
  // 结构移植 rigplane handler.py IC-7610 hardcoded dump_state（hamlib v0
  // positional 格式，netrigctl.c 逐行 atol）；机型数据改 IC-705 化：
  // RX 30k–200M + 400–470M（原厂 ini），TX 业余段合并区间，模型号 3073。
  // Mode 位表/has_get_func/has_get_level 位运算注释见上游源（MIT）。
  return QByteArray(
      "0\n"                                 // protocol version
      "3073\n"                              // rig model（IC-705 化）
      "1\n"                                 // ITU region
      "30000.000000 199999999.000000 0x1ff -1 -1 0x3 0xf\n"
      "400000000.000000 470000000.000000 0x1ff -1 -1 0x3 0xf\n"
      "0 0 0 0 0 0 0\n"                     // end of RX ranges
      "1800000.000000 200000000.000000 0x1ff 5000 100000 0x3 0xf\n"
      "400000000.000000 470000000.000000 0x1ff 5000 100000 0x3 0xf\n"
      "0 0 0 0 0 0 0\n"                     // end of TX ranges
      "0x1ff 1\n"                           // tuning steps
      "0 0\n"
      "0x1ff 3000\n0x1ff 2400\n0x1ff 1800\n0 0\n"
      "9999\n"                              // max_rit
      "9999\n"                              // max_xit
      "0\n"                                 // max_ifshift
      "0\n"                                 // announces
      "12 20 0\n"                           // preamp
      "6 12 18 0\n"                         // attenuator
      "0x00011B3E\n"                        // has_get_func（rigplane 同表）
      "0x00011B3E\n"                        // has_set_func
      "0x5401791B\n"                        // has_get_level
      "0x0001791B\n"                        // has_set_level
      "0\n"                                 // has_get_parm
      "0\n");                               // has_set_parm
}

}  // namespace radio::bus
