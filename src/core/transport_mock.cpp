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
      const uint8_t p[] = {0x03, static_cast<uint8_t>(sim_.smeter_raw & 0xFF)};
      pushResponse(civ::kCmdReadSmeter, p);
      return true;
    }
    case civ::kCmdReadPtt: {
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
      // 白名单门禁的模拟侧镜像：越界 NAK（真机语义：拒答/NAK）
      if (!hz || !isAllowedFreq(*hz)) {
        pushResponse(civ::kNak, {});
      } else {
        sim_.freq_hz = *hz;
        pushResponse(civ::kAck, {});
      }
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
