// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fake_rsba1_server.h"

#include <QHostAddress>
#include <QUdpSocket>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>

#include "core/audio_stream.h"
#include "core/rsba1_proto.h"

namespace rsba1 = radio::core::rsba1;

namespace {

constexpr int kPollMs = 2;
constexpr uint32_t kCtrlServerSid = 0x0A0B0C0D;
constexpr uint32_t kSerialServerSid = 0x0B0C0D0E;
constexpr uint32_t kAudioServerSid = 0x0C0D0E0F;

uint16_t le16(const std::vector<uint8_t>& d, size_t off) {
  return static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
}
uint32_t be32(const std::vector<uint8_t>& d, size_t off) {
  return (static_cast<uint32_t>(d[off]) << 24) |
         (static_cast<uint32_t>(d[off + 1]) << 16) |
         (static_cast<uint32_t>(d[off + 2]) << 8) | d[off + 3];
}
void putBe32(std::vector<uint8_t>& d, size_t off, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    d[off + static_cast<size_t>(3 - i)] = static_cast<uint8_t>(v >> (8 * i));
}

}  // namespace

FakeRsba1Server::FakeRsba1Server() = default;

FakeRsba1Server::~FakeRsba1Server() { stop(); }

bool FakeRsba1Server::start(std::string* error) {
  if (thread_.joinable()) {
    if (error) *error = "假服务端已启动";
    return false;
  }
  run_auth_ = auth_id;
  run_a8_ = a8_reply_id;
  run_server_sid_ = new_server_sid;
  run_client_sid_ = new_client_sid;
  run_model_ = model;
  stop_ = false;

  auto ports = std::make_shared<std::promise<std::array<uint16_t, 3>>>();
  auto fut = ports->get_future();
  thread_ = std::thread([this, ports] {
    QUdpSocket ctrl, ser, aud;
    if (!ctrl.bind(QHostAddress::LocalHost, 0) ||
        !ser.bind(QHostAddress::LocalHost, 0) ||
        !aud.bind(QHostAddress::LocalHost, 0)) {
      ports->set_value({0, 0, 0});
      return;
    }
    // 服务线程不用 waitForReadyRead：Windows 上 Qt 的 UDP 阻塞等待会把
    // socket 打成错误态（随后 close），非阻塞轮询才是这里的正确姿势。
    ctrl_ = Chan{Kind::Ctrl, &ctrl, kCtrlServerSid};
    serial_ = Chan{Kind::Serial, &ser, kSerialServerSid};
    audio_ = Chan{Kind::Audio, &aud, kAudioServerSid};
    ports->set_value({ctrl.localPort(), ser.localPort(), aud.localPort()});
    while (!stop_.load()) {
      pump(ctrl_);
      pump(serial_);
      pump(audio_);
      flushInjections();
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
  });

  if (fut.wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
    if (error) *error = "假服务端线程未就绪";
    stop();
    return false;
  }
  const auto p = fut.get();
  if (p[0] == 0) {
    if (error) *error = "UDP 绑定 127.0.0.1 失败";
    stop();
    return false;
  }
  ctrl_port_ = p[0];
  serial_port_ = p[1];
  audio_port_ = p[2];
  return true;
}

void FakeRsba1Server::stop() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
}

void FakeRsba1Server::pump(Chan& ch) {
  while (ch.sock->hasPendingDatagrams()) {
    char buf[2048];
    QHostAddress from;
    quint16 port = 0;
    const qint64 n = ch.sock->readDatagram(buf, sizeof buf, &from, &port);
    if (n <= 0) break;
    ch.peer = from;
    ch.peer_port = port;
    handlePacket(ch, std::vector<uint8_t>(buf, buf + n));
  }
}

void FakeRsba1Server::send(Chan& ch, std::span<const uint8_t> data) {
  if (ch.peer_port == 0) return;
  ch.sock->writeDatagram(reinterpret_cast<const char*>(data.data()),
                         static_cast<qint64>(data.size()), ch.peer, ch.peer_port);
}

void FakeRsba1Server::handlePacket(Chan& ch, const std::vector<uint8_t>& d) {
  if (d.size() < 16) return;
  if (d.size() == 16) {
    handleControlPlane(ch, d);
    return;
  }
  switch (ch.kind) {
    case Kind::Audio:
      handleAudioPayload(d);
      return;
    case Kind::Serial:
      handleSerialPayload(d);
      return;
    case Kind::Ctrl:
      break;
  }
  if (d.size() == 0x80) {  // login：记下客户端 SID，回 0x60 应答
    ch.client_sid = be32(d, 8);
    send(ch, buildLoginReply(run_auth_));
    return;
  }
  if (d.size() == 0x40) {  // auth（0x02/0x05）/ deauth（0x01）；magic 在 biz 头 +5
    const uint8_t magic = d[0x15];
    if (magic == 0x01) {
      std::lock_guard lk(mu_);
      got_deauth_ = true;
      return;
    }
    send(ch, buildAuthReply(magic));
    if (magic == 0x05) send(ch, buildA8Packet(run_a8_));  // 巩固完成 → 下推 0xA8
    return;
  }
  if (d.size() == 0x90) {  // ConnectTrans：下发新 SID + 设备名
    send(ch, buildConnectTransReply(run_server_sid_, run_client_sid_, run_auth_,
                                    run_model_));
    ch.server_sid = run_server_sid_;
    return;
  }
}

void FakeRsba1Server::handleControlPlane(Chan& ch, const std::vector<uint8_t>& d) {
  switch (le16(d, 4)) {
    case 0x03:  // pkt3 → pkt4（[8:12] = 本端 SID，客户端据此得到 remoteSID）
      ch.client_sid = be32(d, 8);
      send(ch, rsba1::cmdTransportHeader(0x10, 0x04, 0, ch.server_sid, ch.client_sid));
      return;
    case 0x06:  // pkt6 → 应答
      send(ch, rsba1::cmdTransportHeader(0x10, 0x06, 0, ch.server_sid, ch.client_sid));
      return;
    case 0x05: {  // disconnect（关闭五件套逐信道到达）
      std::lock_guard lk(mu_);
      ++disconnects_;
      return;
    }
    default:
      return;
  }
}

void FakeRsba1Server::handleSerialPayload(const std::vector<uint8_t>& d) {
  auto f = rsba1::parseSerialFrame(
      std::span<const uint8_t>(d.data() + 16, d.size() - 16));
  if (!f) return;
  std::lock_guard lk(mu_);
  if (!f->bulk) {  // serial open(0x05) / close(0x00)
    if (f->payload.size() == 1 && f->payload[0] == 0x05) serial_opened_ = true;
    if (f->payload.size() == 1 && f->payload[0] == 0x00) serial_closed_ = true;
    return;
  }
  civ_frames_.push_back(f->payload);
}

void FakeRsba1Server::handleAudioPayload(const std::vector<uint8_t>& d) {
  if (d.size() < 24) return;
  const bool part1 = d[0] == 0x6c && d[1] == 0x05;
  const bool part2 = d[0] == 0x44 && d[1] == 0x02;
  if (!part1 && !part2) return;
  std::lock_guard lk(mu_);
  audio_packets_.push_back(d);
  if (part1) {
    audio_part1_.assign(d.begin() + 24, d.end());
  } else if (!audio_part1_.empty()) {
    std::vector<uint8_t> frame = audio_part1_;
    frame.insert(frame.end(), d.begin() + 24, d.end());
    audio_frames_.push_back(std::move(frame));
    audio_part1_.clear();
  }
}

void FakeRsba1Server::flushInjections() {
  std::lock_guard lk(mu_);
  auto flush = [this](std::vector<std::vector<uint8_t>>& q, Chan& ch) {
    if (q.empty() || ch.peer_port == 0) return;  // 对端未握手则继续排队
    for (const auto& p : q) send(ch, p);
    q.clear();
  };
  flush(inject_civ_, serial_);
  flush(inject_audio_, audio_);
}

void FakeRsba1Server::sendCiv(std::span<const uint8_t> frame) {
  auto sf = rsba1::buildSerialFrame(frame, civ_sseq_++, true);
  // Serial 信道 wire 头为全 LE（客户端 dispatchSerial 不校验 SID）
  auto wire = rsba1::serWireHeader(0x00, 0, kSerialServerSid, 0, sf.size());
  std::vector<uint8_t> pkt = wire;
  pkt.insert(pkt.end(), sf.begin(), sf.end());
  std::lock_guard lk(mu_);
  inject_civ_.push_back(std::move(pkt));
}

void FakeRsba1Server::sendAudio(std::span<const uint8_t> pcm, uint16_t seq) {
  const bool part2 = pcm.size() <= rsba1::kAudioPart2Pcm;
  auto pkt = buildAudioPacket(pcm, seq, part2);
  std::lock_guard lk(mu_);
  inject_audio_.push_back(std::move(pkt));
}

std::vector<std::vector<uint8_t>> FakeRsba1Server::clientCivFrames() {
  std::lock_guard lk(mu_);
  return civ_frames_;
}

std::vector<std::vector<uint8_t>> FakeRsba1Server::clientAudioPackets() {
  std::lock_guard lk(mu_);
  return audio_packets_;
}

std::vector<std::vector<uint8_t>> FakeRsba1Server::clientAudioFrames() {
  std::lock_guard lk(mu_);
  return audio_frames_;
}

bool FakeRsba1Server::serialOpened() {
  std::lock_guard lk(mu_);
  return serial_opened_;
}

bool FakeRsba1Server::serialClosed() {
  std::lock_guard lk(mu_);
  return serial_closed_;
}

bool FakeRsba1Server::gotDeauth() {
  std::lock_guard lk(mu_);
  return got_deauth_;
}

int FakeRsba1Server::disconnectCount() {
  std::lock_guard lk(mu_);
  return disconnects_;
}

// ---- 应答报文构造（布局对齐 tests/fixtures/*.hex）----

std::vector<uint8_t> FakeRsba1Server::buildLoginReply(
    std::span<const uint8_t, 6> auth6) {
  std::vector<uint8_t> pkt(0x60, 0);
  pkt[0] = 0x60;  // totalLen
  pkt[6] = 0x01;  // seq=1（与 udp_login_resp_ok.hex 一致）
  std::memcpy(pkt.data() + 26, auth6.data(), 6);
  // [48:52] 结果码 0 = 通过
  return pkt;
}

std::vector<uint8_t> FakeRsba1Server::buildAuthReply(uint8_t magic) {
  std::vector<uint8_t> pkt(0x40, 0);
  pkt[0] = 0x40;
  pkt[21] = magic;  // rsba1-core parse_auth_reply_magic：abs[21]
  return pkt;
}

std::vector<uint8_t> FakeRsba1Server::buildA8Packet(
    std::span<const uint8_t, 16> reply16) {
  std::vector<uint8_t> pkt(0xA8, 0);
  pkt[0] = 0xA8;
  std::memcpy(pkt.data() + 66, reply16.data(), 16);  // extractA8ReplyId: abs[66:82]
  return pkt;
}

std::vector<uint8_t> FakeRsba1Server::buildConnectTransReply(
    uint32_t server_sid, uint32_t client_sid,
    std::span<const uint8_t, 6> auth6, const std::string& model) {
  std::vector<uint8_t> pkt(0x90, 0);
  pkt[0] = 0x90;
  putBe32(pkt, 8, server_sid);
  putBe32(pkt, 12, client_sid);
  std::memcpy(pkt.data() + 26, auth6.data(), 6);
  std::memcpy(pkt.data() + 64, model.data(),
              std::min<size_t>(model.size(), 16));
  pkt[96] = 0x01;  // ok
  return pkt;
}

std::vector<uint8_t> FakeRsba1Server::buildAudioPacket(
    std::span<const uint8_t> pcm, uint16_t seq, bool part2) {
  std::vector<uint8_t> pkt;
  pkt.reserve(24 + pcm.size());
  pkt.insert(pkt.end(), {part2 ? uint8_t{0x44} : uint8_t{0x6c},
                         part2 ? uint8_t{0x02} : uint8_t{0x05}});
  pkt.resize(24, 0);  // 其余头字段按 sendPart1/2 布局补齐
  pkt[6] = static_cast<uint8_t>(seq & 0xFF);  // RX 侧唯一解析字段（LE）
  pkt[7] = static_cast<uint8_t>(seq >> 8);
  pkt[16] = 0x80;
  pkt[18] = static_cast<uint8_t>((seq - 1) >> 8);
  pkt[19] = static_cast<uint8_t>(seq - 1);
  pkt[22] = static_cast<uint8_t>(pcm.size() >> 8);
  pkt[23] = static_cast<uint8_t>(pcm.size() & 0xFF);
  pkt.insert(pkt.end(), pcm.begin(), pcm.end());
  return pkt;
}
