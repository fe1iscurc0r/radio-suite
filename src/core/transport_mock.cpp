// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/transport_mock.h"

#include "core/bands.h"
#include "core/civ.h"

namespace radio::core {

std::vector<uint8_t> ICivTransport::frame(uint8_t to, uint8_t from, uint8_t cmd,
                                          std::span<const uint8_t> payload) {
  std::vector<uint8_t> body;
  body.reserve(payload.size() + 1);
  body.push_back(cmd);
  body.insert(body.end(), payload.begin(), payload.end());
  return civ::buildFrame(to, from, body);
}

MockTransport::MockTransport(SimConfig cfg) : sim_(cfg) {}

bool MockTransport::open(std::string*) {
  open_ = true;
  return true;
}

void MockTransport::close() { open_ = false; }

bool MockTransport::isOpen() const { return open_; }

void MockTransport::pushResponse(uint8_t cmd, std::span<const uint8_t> payload) {
  pending_.push_back(frame(last_controller_, sim_.radio_addr, cmd, payload));
}

bool MockTransport::send(std::span<const uint8_t> data) {
  if (!open_) return false;
  auto parsed = civ::parseFrame(data);
  if (!parsed) return false;  // 帧格式错误：链路层送达，协议层忽略

  last_controller_ = parsed->from;  // 应答目的地址 = 请求源地址

  switch (parsed->cmd) {
    case civ::kCmdReadFreq: {
      auto bcd = civ::freqToBcd5(sim_.freq_hz);
      if (!bcd) return false;
      pushResponse(civ::kCmdReadFreq,
                   std::span<const uint8_t>(bcd->data(), bcd->size()));
      return true;
    }
    case civ::kCmdReadMode: {
      const uint8_t p[] = {static_cast<uint8_t>(sim_.mode),
                           static_cast<uint8_t>(sim_.filter)};
      pushResponse(civ::kCmdReadMode, p);
      return true;
    }
    case civ::kCmdReadSmeter: {
      if (!parsed->payload.empty() && parsed->payload[0] == 0x11) {
        // 复合表：TX 时给典型值（Po≈5W→50, SWR≈1.2→12），RX 全 0；量纲 M1 真机标定
        const int po = sim_.ptt ? 50 : 0;
        const int swr = sim_.ptt ? 12 : 0;
        const int alc = sim_.ptt ? 30 : 0;
        const uint8_t p[] = {0x11,
                             static_cast<uint8_t>(po >> 8),
                             static_cast<uint8_t>(po & 0xFF),
                             static_cast<uint8_t>(swr >> 8),
                             static_cast<uint8_t>(swr & 0xFF),
                             static_cast<uint8_t>(alc & 0xFF)};
        pushResponse(civ::kCmdReadSmeter, p);
        return true;
      }
      const uint8_t p[] = {0x03, static_cast<uint8_t>(sim_.smeter_raw & 0xFF)};
      pushResponse(civ::kCmdReadSmeter, p);
      return true;
    }    case civ::kCmdReadPtt: {
      const uint8_t p[] = {0x0C, static_cast<uint8_t>(sim_.ptt ? 1 : 0)};
      pushResponse(civ::kCmdReadPtt, p);
      return true;
    }
    case civ::kCmdSetFreq: {
      if (parsed->payload.size() != 5) {
        pushResponse(civ::kNak, {});
        return true;
      }
      auto hz = civ::bcdToFreq(parsed->payload);
      // 接收调谐按宽 RX 范围（广播/航空可收听）；发射门禁在 SafetyGate
      if (!hz || !isRxAllowed(*hz)) {
        pushResponse(civ::kNak, {});
      } else {
        sim_.freq_hz = *hz;
        pushResponse(civ::kAck, {});
      }
      return true;
    }
    case civ::kCmdAtt: {
      if (parsed->payload.empty()) {  // 裸查询 → 回当前值
        const uint8_t p[] = {static_cast<uint8_t>(sim_.att_20db ? 0x20 : 0x00)};
        pushResponse(civ::kCmdAtt, p);
        return true;
      }
      // 真机语义：ATT 仅 HF/50MHz（1.8–54 MHz）可设，其余 NAK
      const bool in_hf = sim_.freq_hz >= 1'800'000 && sim_.freq_hz <= 54'000'000;
      if (!in_hf || parsed->payload.size() != 1 ||
          (parsed->payload[0] != 0x00 && parsed->payload[0] != 0x20)) {
        pushResponse(civ::kNak, {});
      } else {
        sim_.att_20db = parsed->payload[0] == 0x20;
        pushResponse(civ::kAck, {});
      }
      return true;
    }
    case civ::kCmdDuplex: {
      if (parsed->payload.empty()) {  // 裸查询：真机语义 simplex 归一为 0x00
        const uint8_t p[] = {static_cast<uint8_t>(sim_.duplex == 0x10 ? 0x00 : sim_.duplex)};
        pushResponse(civ::kCmdDuplex, p);
        return true;
      }
      if (parsed->payload.size() != 1 ||
          (parsed->payload[0] < 0x10 || parsed->payload[0] > 0x12)) {
        pushResponse(civ::kNak, {});
      } else {
        sim_.duplex = parsed->payload[0];
        pushResponse(civ::kAck, {});
      }
      return true;
    }
    case civ::kCmdSw16: {
      if (parsed->payload.empty()) return true;
      const uint8_t sub = parsed->payload[0];
      if (parsed->payload.size() == 1) {  // 读
        uint8_t val = 0;
        switch (sub) {
          case civ::kSub16Pamp: val = static_cast<uint8_t>(sim_.pamp); break;
          case civ::kSub16Agc:  val = static_cast<uint8_t>(sim_.agc); break;
          case civ::kSub16Nb:   val = sim_.nb ? 1 : 0; break;
          case civ::kSub16Nr:   val = sim_.nr ? 1 : 0; break;
          case civ::kSub16Moni: val = sim_.moni ? 1 : 0; break;
          case civ::kSub16NotchAuto: val = sim_.notch_auto ? 1 : 0; break;
          case civ::kSub16Vox: val = sim_.vox ? 1 : 0; break;
          default: return true;  // 未模拟的子命令静默
        }
        const uint8_t p[] = {sub, val};
        pushResponse(civ::kCmdSw16, p);
        return true;
      }
      // 写：值合法性按子命令校验后存并 ACK
      if (parsed->payload.size() != 2) {
        pushResponse(civ::kNak, {});
        return true;
      }
      const uint8_t val = parsed->payload[1];
      bool ok = true;
      switch (sub) {
        case civ::kSub16Pamp: ok = val <= 2; if (ok) sim_.pamp = val; break;
        case civ::kSub16Agc:  ok = val >= 1 && val <= 3; if (ok) sim_.agc = val; break;
        case civ::kSub16Nb:   ok = val <= 1; if (ok) sim_.nb = val != 0; break;
        case civ::kSub16Nr:   ok = val <= 1; if (ok) sim_.nr = val != 0; break;
        case civ::kSub16Moni: ok = val <= 1; if (ok) sim_.moni = val != 0; break;
        case civ::kSub16NotchAuto: ok = val <= 1; if (ok) sim_.notch_auto = val != 0; break;
        case civ::kSub16Vox:  ok = val <= 1; if (ok) sim_.vox = val != 0; break;
        default: return true;  // 未模拟的子命令静默忽略
      }
      pushResponse(ok ? civ::kAck : civ::kNak, {});
      return true;
    }
    case civ::kCmdSetMode: {
      if (parsed->payload.size() != 2 || !civ::modeFromCode(parsed->payload[0])) {
        pushResponse(civ::kNak, {});
      } else {
        sim_.mode = static_cast<civ::Mode>(parsed->payload[0]);
        sim_.filter = parsed->payload[1];
        pushResponse(civ::kAck, {});
      }
      return true;
    }
    case civ::kCmdPtt: {
      if (parsed->payload.size() != 2 || parsed->payload[0] != 0x00) {
        pushResponse(civ::kNak, {});
      } else {
        sim_.ptt = parsed->payload[1] != 0x00;
        pushResponse(civ::kAck, {});
      }
      return true;
    }
    default:
      return true;  // 未实现命令：静默忽略（与真机对未知 cmd 的行为一致）
  }
}

size_t MockTransport::poll() {
  std::vector<std::vector<uint8_t>> out;
  out.swap(pending_);
  for (const auto& f : out) emitFrame(f);
  return out.size();
}

}  // namespace radio::core
