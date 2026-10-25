// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/transport_udp.h"

#include <QDateTime>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QUdpSocket>
#include <cstring>

#include "core/rsba1_proto.h"

namespace radio::core {
// 本编译单元已处于 radio::core 内：rsba1:: 即 radio::core::rsba1，无需别名

namespace {

uint32_t ipToBe(const QHostAddress& a) {
  const quint32 v = a.toIPv4Address();
  return static_cast<uint32_t>(v);
}

int64_t nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

bool isPkt4Reply(const std::vector<uint8_t>& d) {
  return d.size() == 16 && d[4] == 0x04 && d[5] == 0x00;
}
bool isPkt6Reply(const std::vector<uint8_t>& d) {
  return d.size() == 16 && d[4] == 0x06 && d[5] == 0x00;
}
bool isLoginReply(const std::vector<uint8_t>& d) {
  return rsba1::parseLoginResponse(d).has_value();
}
bool isAuthReply(const std::vector<uint8_t>& d) {
  return rsba1::parseAuthReplyMagic(d).has_value();
}
bool isA8(const std::vector<uint8_t>& d) { return rsba1::isA8Packet(d); }
bool isConnectTransReply(const std::vector<uint8_t>& d) {
  return rsba1::parseConnectTransResponse(d).has_value();
}

// BE 读 uint32（ctrl 域）
uint32_t be32(const std::vector<uint8_t>& d, size_t off) {
  return (static_cast<uint32_t>(d[off]) << 24) | (static_cast<uint32_t>(d[off + 1]) << 16) |
         (static_cast<uint32_t>(d[off + 2]) << 8) | d[off + 3];
}

}  // namespace

Rsba1UdpTransport::Rsba1UdpTransport(Config cfg) : cfg_(std::move(cfg)) {}

Rsba1UdpTransport::~Rsba1UdpTransport() { close(); }

void Rsba1UdpTransport::log(const std::string& line) {
  if (!log_.empty()) log_ += "\n";
  log_ += line;
}

bool Rsba1UdpTransport::handshake(QUdpSocket& sock, uint32_t& local_sid,
                                  uint32_t& remote_sid, const char* name,
                                  std::string* error) {
  // connectToHost 之后 localAddress/localPort 即为实际源（localSID 派生）
  local_sid = rsba1::makeLocalSid(ipToBe(sock.localAddress()),
                                  static_cast<uint16_t>(sock.localPort()));
  remote_sid = 0;

  auto pkt3 = rsba1::buildPkt3(local_sid);
  sock.write(reinterpret_cast<const char*>(pkt3.data()), pkt3.size());
  sock.write(reinterpret_cast<const char*>(pkt3.data()), pkt3.size());  // 连发两遍
  auto p4 = waitPacket(sock, cfg_.handshake_timeout_ms, isPkt4Reply, "pkt4");
  if (p4.empty()) {
    if (error) *error = std::string(name) + ": 未收到 pkt4";
    return false;
  }
  remote_sid = be32(p4, 8);
  log(std::string(name) + ": pkt4 ← remoteSID=0x" +
      [&] { char b[16]; snprintf(b, sizeof b, "%08X", remote_sid); return std::string(b); }());

  auto pkt6 = rsba1::buildPkt6(local_sid, remote_sid);
  sock.write(reinterpret_cast<const char*>(pkt6.data()), pkt6.size());
  sock.write(reinterpret_cast<const char*>(pkt6.data()), pkt6.size());
  if (waitPacket(sock, cfg_.handshake_timeout_ms, isPkt6Reply, "pkt6").empty()) {
    if (error) *error = std::string(name) + ": 未收到 pkt6 应答";
    return false;
  }
  return true;
}

std::vector<uint8_t> Rsba1UdpTransport::waitPacket(
    QUdpSocket& sock, int timeout_ms,
    bool (*pred)(const std::vector<uint8_t>&), const char* what) {
  const int64_t deadline = nowMs() + timeout_ms;
  std::vector<uint8_t> buf(0x1000);
  while (nowMs() < deadline) {
    if (!sock.waitForReadyRead(std::max<int>(20, static_cast<int>(deadline - nowMs()))))
      continue;
    while (sock.hasPendingDatagrams()) {
      const qint64 n = sock.readDatagram(reinterpret_cast<char*>(buf.data()),
                                         static_cast<qint64>(buf.size()));
      if (n <= 0) continue;
      std::vector<uint8_t> data(buf.begin(), buf.begin() + n);
      if (pred(data)) return data;
      // 非目标包：serial 信道上可能是空探测/keepalive，顺手处理
      if (&sock == ser_ && state_ == State::Connected) dispatchSerial(std::move(data));
      else if (&sock == ctrl_ && rsba1::isPkt7(data) && data[16] == 0x00) {
        std::array<uint8_t, 4> rid{};
        std::memcpy(rid.data(), data.data() + 17, 4);
        auto reply = rsba1::buildPkt7(ctrl_local_, ctrl_remote_,
                                      static_cast<uint16_t>(data[6] + 1), rid);
        sock.write(reinterpret_cast<const char*>(reply.data()), reply.size());
      }
    }
  }
  log(std::string("wait ") + what + " 超时");
  return {};
}

bool Rsba1UdpTransport::open(std::string* error) {
  if (state_ == State::Connected) return true;
  const auto fail = [&](const std::string& msg) {
    state_ = State::Failed;
    if (error) *error = msg;
    log("FAIL: " + msg);
    close();
    return false;
  };

  ctrl_ = new QUdpSocket();
  ctrl_->connectToHost(QString::fromStdString(cfg_.host), cfg_.cmd_port);
  if (!ctrl_->waitForConnected(3000)) return fail("ctrl 信道无法连接 " + cfg_.host);

  state_ = State::CtrlHandshake;
  if (!handshake(*ctrl_, ctrl_local_, ctrl_remote_, "ctrl", error))
    return fail(error ? *error : "ctrl 握手失败");
  if (ctrl_local_ == 0)
    ctrl_local_ = rsba1::makeLocalSid(ipToBe(ctrl_->localAddress()),
                                      static_cast<uint16_t>(ctrl_->localPort()));

  state_ = State::Login;
  std::array<uint8_t, 2> aid{};
  const quint32 rnd = QRandomGenerator::global()->generate();
  aid[0] = static_cast<uint8_t>(rnd & 0xFF);
  aid[1] = static_cast<uint8_t>((rnd >> 8) & 0xFF);
  auto login = rsba1::buildLoginRequest(cfg_.username, cfg_.password, ctrl_local_,
                                        ctrl_remote_, ++ctrl_seq_, 0, aid);
  ctrl_->write(reinterpret_cast<const char*>(login.data()), login.size());
  auto lresp = waitPacket(*ctrl_, cfg_.handshake_timeout_ms, isLoginReply, "login");
  auto lr = rsba1::parseLoginResponse(lresp);
  if (!lr || !lr->ok)
    return fail(lr ? ("登录被拒 result=" + std::to_string(lr->result_code))
                   : "登录应答超时");
  auth_id_ = lr->auth_id;
  log("login: OK");

  for (const uint8_t magic : {0x02, 0x05}) {
    auto auth = rsba1::buildAuthRequest(magic, ctrl_local_, ctrl_remote_,
                                        ++ctrl_seq_, 0x10 + magic, auth_id_);
    ctrl_->write(reinterpret_cast<const char*>(auth.data()), auth.size());
    if (waitPacket(*ctrl_, cfg_.handshake_timeout_ms, isAuthReply, "auth").empty())
      return fail("auth 巩固包无应答");
  }
  log("auth: OK");

  auto a8 = waitPacket(*ctrl_, 5000, isA8, "0xA8");
  auto rid16 = rsba1::extractA8ReplyId(a8);
  if (!rid16) return fail("未等到电台 0xA8 包");

  rsba1::ConnectTransParams ctp;
  ctp.local_sid = ctrl_local_;
  ctp.remote_sid = ctrl_remote_;
  ctp.outer_seq = ++ctrl_seq_;
  ctp.inner_seq = 0x30;
  ctp.auth_id6 = auth_id_;
  ctp.a8_reply_id16 = std::span<const uint8_t>(rid16->data(), rid16->size());
  ctp.username = cfg_.username;
  ctp.serial_port = cfg_.ser_port;
  ctp.audio_port = cfg_.aud_port;
  auto ct = rsba1::buildConnectTransRequest(ctp);
  ctrl_->write(reinterpret_cast<const char*>(ct.data()), ct.size());
  auto ctresp = waitPacket(*ctrl_, cfg_.handshake_timeout_ms, isConnectTransReply,
                           "connectTrans");
  auto ctr = rsba1::parseConnectTransResponse(ctresp);
  if (!ctr || !ctr->ok) return fail("ConnectTrans 被拒");
  ctrl_local_ = ctr->new_local_sid;    // 服务器为本端新分配的 SID
  ctrl_remote_ = ctr->new_remote_sid;
  log("connectTrans: OK dev=" + ctr->dev_name);

  // ---- Serial 信道 ----
  state_ = State::SerialHandshake;
  ser_ = new QUdpSocket();
  ser_->connectToHost(QString::fromStdString(cfg_.host), cfg_.ser_port);
  if (!ser_->waitForConnected(3000)) return fail("serial 信道无法连接");
  if (!handshake(*ser_, ser_local_, ser_remote_, "serial", error))
    return fail(error ? *error : "serial 握手失败");

  // serial open（透传闸门，单字节 0x05，bulk=false）
  const uint8_t open_magic[] = {0x05};
  auto open_frame = rsba1::buildSerialFrame(open_magic, sseq_++, false);
  std::vector<uint8_t> open_pkt = rsba1::cmdTransportHeader(
      static_cast<uint32_t>(0x10 + open_frame.size()), 0x00, ser_seq_++,
      ser_local_, ser_remote_);
  open_pkt.insert(open_pkt.end(), open_frame.begin(), open_frame.end());
  ser_->write(reinterpret_cast<const char*>(open_pkt.data()), open_pkt.size());
  serial_opened_ = true;
  log("serial: open magic=0x05");

  // ---- Audio 信道（50003 双通路：接 kappanhang 语义音频，不再只是空握手）----
  aud_ = new QUdpSocket();
  aud_->connectToHost(QString::fromStdString(cfg_.host), cfg_.aud_port);
  if (aud_->waitForConnected(3000) &&
      handshake(*aud_, aud_local_, aud_remote_, "audio", nullptr)) {
    audio_tx_.setSids(aud_local_, aud_remote_);
    log("audio: 双通路就绪（RX 重排 + TX 打包，48kHz s16le/20ms）");
  } else {
    log("audio: 握手未完成（音频不可用，PTT 语音将无声）");
  }

  state_ = State::Connected;
  last_reauth_ms_ = nowMs();
  log("link: CONNECTED");
  return true;
}

bool Rsba1UdpTransport::send(std::span<const uint8_t> frame) {
  if (state_ != State::Connected || !ser_) return false;
  auto sf = rsba1::buildSerialFrame(frame, sseq_++, true);
  auto pkt = rsba1::cmdTransportHeader(
      static_cast<uint32_t>(0x10 + sf.size()), 0x00, ser_seq_++,
      ser_local_, ser_remote_);
  pkt.insert(pkt.end(), sf.begin(), sf.end());
  ser_tx_buf_[static_cast<uint16_t>(ser_seq_ - 1)] = pkt;  // 供 RETRANSMIT
  if (ser_tx_buf_.size() > 256) ser_tx_buf_.erase(ser_tx_buf_.begin());
  const qint64 n = ser_->write(reinterpret_cast<const char*>(pkt.data()), pkt.size());
  return n == static_cast<qint64>(pkt.size());
}

bool Rsba1UdpTransport::sendAudioFrame(std::span<const uint8_t> pcm20ms) {
  if (state_ != State::Connected || !aud_) return false;
  audio_tx_.setSids(aud_local_, aud_remote_);
  for (const auto& pkt : audio_tx_.packFrame(pcm20ms))
    aud_->write(reinterpret_cast<const char*>(pkt.data()),
                static_cast<qint64>(pkt.size()));
  return true;
}

size_t Rsba1UdpTransport::poll() {
  if (state_ != State::Connected) return 0;
  std::vector<uint8_t> buf(0x1000);
  for (QUdpSocket* sock : {ser_, ctrl_, aud_}) {
    if (!sock) continue;
    while (sock->hasPendingDatagrams()) {
      const qint64 n = sock->readDatagram(reinterpret_cast<char*>(buf.data()),
                                          static_cast<qint64>(buf.size()));
      if (n <= 0) break;
      std::vector<uint8_t> data(buf.begin(), buf.begin() + n);
      if (sock == ser_) dispatchSerial(std::move(data));
      else if (sock == ctrl_) dispatchCtrl(std::move(data));
      else {
        // 50003：音频下行（kappanhang 语义：识别→seq 重排→PCM 上抛）
        auto ev = audio_rx_.feed(data);
        if (ev.delivered && audio_cb_ && ev.pcm.size() % 2 == 0) {
          std::span<const int16_t> pcm(
              reinterpret_cast<const int16_t*>(ev.pcm.data()),
              ev.pcm.size() / 2);
          audio_cb_(pcm);
        }
      }
    }
  }
  // 控制信道周期再鉴权（阈值 90s，取 60s 周期）
  if (last_reauth_ms_ != 0 && nowMs() - last_reauth_ms_ > 60'000) {
    auto auth = rsba1::buildAuthRequest(0x05, ctrl_local_, ctrl_remote_,
                                        ++ctrl_seq_, 0x15, auth_id_);
    ctrl_->write(reinterpret_cast<const char*>(auth.data()), auth.size());
    last_reauth_ms_ = nowMs();
  }
  const size_t n = delivered_;
  delivered_ = 0;
  return n;
}

void Rsba1UdpTransport::dispatchCtrl(std::vector<uint8_t> data) {
  // 电台 keepalive 请求 → 回显应答
  if (rsba1::isPkt7(data) && data[16] == 0x00) {
    std::array<uint8_t, 4> rid{};
    std::memcpy(rid.data(), data.data() + 17, 4);
    auto reply = rsba1::buildPkt7(ctrl_local_, ctrl_remote_,
                                  static_cast<uint16_t>(data[6] + 1), rid);
    ctrl_->write(reinterpret_cast<const char*>(reply.data()), reply.size());
  }
}

void Rsba1UdpTransport::dispatchSerial(std::vector<uint8_t> data) {
  if (data.size() < 16) return;
  // 重传请求：载荷 = 1+ 个 LE uint16 wire seq
  if (data[4] == 0x01 && data[5] == 0x00) {
    const size_t n = (data.size() - 16) / 2;
    for (size_t i = 0; i < n; ++i) {
      const uint16_t seq = static_cast<uint16_t>(data[16 + i * 2] |
                                                 (data[17 + i * 2] << 8));
      auto it = ser_tx_buf_.find(seq);
      if (it != ser_tx_buf_.end())
        ser_->write(reinterpret_cast<const char*>(it->second.data()),
                    it->second.size());
    }
    return;
  }
  // CI-V 数据：wire 头(16) + Serial 帧(5) + payload
  if (data.size() > 21 && data[4] == 0x00 && data[5] == 0x00) {
    auto frame = rsba1::parseSerialFrame(
        std::span<const uint8_t>(data.data() + 16, data.size() - 16));
    if (frame && frame->bulk && !frame->payload.empty()) {
      emitFrame(frame->payload);
      ++delivered_;
      return;
    }
  }
  // 空探测包：对调 localSID/remoteSID 原样回传（客户端存活确认）
  if (data.size() == 16 && data[4] == 0x00 && data[5] == 0x00) {
    const uint32_t f8 = be32(data, 8), fc = be32(data, 12);
    auto echo = rsba1::cmdTransportHeader(
        0x10, 0x00, static_cast<uint16_t>((static_cast<uint16_t>(data[6]) |
                                           (data[7] << 8)) + 1),
        fc, f8);
    ser_->write(reinterpret_cast<const char*>(echo.data()), echo.size());
  }
}

void Rsba1UdpTransport::close() {
  // 五件套缺一不可（radio_link.close 复刻）
  if (serial_opened_ && ser_) {
    const uint8_t close_magic[] = {0x00};
    auto frame = rsba1::buildSerialFrame(close_magic, sseq_++, false);
    auto pkt = rsba1::cmdTransportHeader(
        static_cast<uint32_t>(0x10 + frame.size()), 0x00, ser_seq_++,
        ser_local_, ser_remote_);
    pkt.insert(pkt.end(), frame.begin(), frame.end());
    ser_->write(reinterpret_cast<const char*>(pkt.data()), pkt.size());
    serial_opened_ = false;
  }
  auto disconnect_chan = [](QUdpSocket* sock, uint32_t local, uint32_t remote) {
    if (!sock) return;
    auto pkt = rsba1::buildDisconnect(local, remote);
    sock->write(reinterpret_cast<const char*>(pkt.data()), pkt.size());
  };
  disconnect_chan(aud_, aud_local_, aud_remote_);  // ② audio 先拆
  if (state_ == State::Connected || state_ == State::Failed) {
    auto deauth = rsba1::buildAuthRequest(0x01, ctrl_local_, ctrl_remote_,
                                          ++ctrl_seq_, 0x20, auth_id_);
    if (ctrl_) ctrl_->write(reinterpret_cast<const char*>(deauth.data()),
                            deauth.size());  // ③ deauth
  }
  disconnect_chan(ser_, ser_local_, ser_remote_);   // ④
  disconnect_chan(ctrl_, ctrl_local_, ctrl_remote_);
  for (QUdpSocket* sock : {aud_, ser_, ctrl_}) {    // ⑤
    if (sock) {
      sock->abort();
      delete sock;
    }
  }
  aud_ = ser_ = ctrl_ = nullptr;
  state_ = State::Idle;
  ser_tx_buf_.clear();
}

}  // namespace radio::core
