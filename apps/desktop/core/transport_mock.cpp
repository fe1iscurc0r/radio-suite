// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/transport_mock.h"

#include "core/bands.h"
#include "core/civ.h"

namespace radio::core {

namespace {

// 2B/3B BCD MSB-first（与 civ.cpp 的编码保持一致，供模拟器回放）
std::vector<uint8_t> bcdMsb(int value, int bytes) {
  std::vector<uint8_t> out(static_cast<size_t>(bytes), 0);
  for (int i = bytes - 1; i >= 0; --i) {
    out[static_cast<size_t>(i)] = static_cast<uint8_t>(
        ((value / 10) % 10) << 4 | (value % 10));
    value /= 100;
  }
  return out;
}

int bcdMsbToInt(std::span<const uint8_t> data) {
  int v = 0;
  for (uint8_t b : data) {
    const int hi = b >> 4, lo = b & 0x0F;
    if (hi > 9 || lo > 9) return -1;
    v = v * 100 + hi * 10 + lo;
  }
  return v;
}

}  // namespace

std::vector<uint8_t> ICivTransport::frame(uint8_t to, uint8_t from, uint8_t cmd,
                                          std::span<const uint8_t> payload) {
  std::vector<uint8_t> body;
  body.reserve(payload.size() + 1);
  body.push_back(cmd);
  body.insert(body.end(), payload.begin(), payload.end());
  return civ::buildFrame(to, from, body);
}

MockTransport::MockTransport() : MockTransport(SimConfig{}) {}

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
    case civ::kCmdReadSmeter: {  // 0x1A 族：S-meter 0x03/复合表 0x11/记忆 0x00/菜单 0x05
      if (!parsed->payload.empty() && parsed->payload[0] == 0x00 &&
          parsed->payload.size() >= 29) {  // 写记忆信道：ch(2) + 26 字节内容
        const auto p = std::span<const uint8_t>(parsed->payload);
        civ::MemoryChannel m;
        m.channel = bcdMsbToInt(p.subspan(1, 2));
        m.scan = p[3];
        const auto hz = civ::bcdToFreq(p.subspan(4, 5));
        const auto mode = bcdMsbToInt(p.subspan(9, 1));
        const auto filter = bcdMsbToInt(p.subspan(10, 1));
        const auto tone = bcdMsbToInt(p.subspan(12, 3));
        const auto tsql = bcdMsbToInt(p.subspan(15, 3));
        if (m.channel < 0 || !hz || mode < 0 || filter < 0 || tone < 0 || tsql < 0 ||
            !civ::modeFromCode(static_cast<uint8_t>(mode))) {
          pushResponse(civ::kNak, {});
          return true;
        }
        m.freq_hz = *hz;
        m.mode = static_cast<civ::Mode>(mode);
        m.filter = filter;
        m.data_mode = (p[11] >> 4) & 0x0F;
        m.tone_mode = p[11] & 0x0F;
        m.tone_freq_hz = static_cast<uint32_t>(tone);
        m.tsql_freq_hz = static_cast<uint32_t>(tsql);
        m.name.assign(reinterpret_cast<const char*>(p.data() + 18), 10);
        while (!m.name.empty() && m.name.back() == '\0') m.name.pop_back();
        sim_.memory[m.channel] = m;
        pushResponse(civ::kAck, {});
        return true;
      }
      if (!parsed->payload.empty() && parsed->payload[0] == 0x00 &&
          parsed->payload.size() >= 3) {
        const int ch = bcdMsbToInt(std::span<const uint8_t>(parsed->payload).subspan(1, 2));
        const auto it = sim_.memory.find(ch);
        if (it == sim_.memory.end()) {
          pushResponse(civ::kNak, {});
          return true;
        }
        const civ::MemoryChannel& m = it->second;
        std::vector<uint8_t> p{0x00};
        for (uint8_t b : bcdMsb(m.channel, 2)) p.push_back(b);
        p.push_back(static_cast<uint8_t>(m.scan));
        for (uint8_t b : civ::freqToBcd5(m.freq_hz).value_or(std::array<uint8_t, 5>{}))
          p.push_back(b);
        p.push_back(static_cast<uint8_t>(((static_cast<int>(m.mode) / 10) % 10) << 4 |
                                         (static_cast<int>(m.mode) % 10)));
        p.push_back(static_cast<uint8_t>(((m.filter / 10) % 10) << 4 | (m.filter % 10)));
        p.push_back(static_cast<uint8_t>(((m.data_mode & 0x0F) << 4) |
                                         (m.tone_mode & 0x0F)));
        for (uint8_t b : bcdMsb(static_cast<int>(m.tone_freq_hz), 3)) p.push_back(b);
        for (uint8_t b : bcdMsb(static_cast<int>(m.tsql_freq_hz), 3)) p.push_back(b);
        std::string name = m.name.substr(0, 10);
        name.resize(10, '\0');
        p.insert(p.end(), name.begin(), name.end());
        p.push_back(0x00);
        pushResponse(civ::kCmdReadSmeter, p);
        return true;
      }
      if (!parsed->payload.empty() && parsed->payload[0] == 0x05 &&
          parsed->payload.size() >= 3) {
        const uint8_t group = parsed->payload[1], item = parsed->payload[2];
        const uint16_t key =
            static_cast<uint16_t>((static_cast<uint16_t>(group) << 8) | item);
        if (parsed->payload.size() == 3) {  // 读
          const auto it = sim_.menu.find(key);
          if (it == sim_.menu.end()) {
            pushResponse(civ::kNak, {});
            return true;
          }
          std::vector<uint8_t> p{0x05, group, item};
          p.insert(p.end(), it->second.begin(), it->second.end());
          pushResponse(civ::kCmdReadSmeter, p);
          return true;
        }
        sim_.menu[key] = std::vector<uint8_t>(parsed->payload.begin() + 3,
                                              parsed->payload.end());
        pushResponse(civ::kAck, {});
        return true;
      }
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
    }    case civ::kCmdPtt: {  // 0x1C 0x00：读 [0x00] / 写 [0x00, 0|1]
      if (parsed->payload.empty() || parsed->payload[0] != 0x00) {
        pushResponse(civ::kNak, {});
      } else if (parsed->payload.size() == 1) {
        const uint8_t p[] = {0x00, static_cast<uint8_t>(sim_.ptt ? 1 : 0)};
        pushResponse(civ::kCmdPtt, p);
      } else if (parsed->payload.size() == 2 && parsed->payload[1] <= 1) {
        sim_.ptt = parsed->payload[1] != 0x00;
        pushResponse(civ::kAck, {});
      } else {
        pushResponse(civ::kNak, {});
      }
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
      // 0x0F 双值域：0x00/0x01 = Split OFF/ON；0x10/0x11/0x12 = simplex/DUP-/DUP+
      if (parsed->payload.size() == 1 && parsed->payload[0] <= 0x01) {
        sim_.split = parsed->payload[0] == 0x01;
        pushResponse(civ::kAck, {});
        return true;
      }
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
    case 0x0C: {  // 读中继频差（3B BCD）
      const uint32_t hz100 = sim_.offset_hz / 100;
      const uint8_t p[] = {static_cast<uint8_t>(((hz100 / 10) % 10) << 4 |
                                                (hz100 % 10)),
                           static_cast<uint8_t>(((hz100 / 1000) % 10) << 4 |
                                                ((hz100 / 100) % 10)),
                           static_cast<uint8_t>((hz100 / 10000) % 10)};
      pushResponse(0x0C, p);
      return true;
    }
    case 0x0D: {  // 写中继频差（3B BCD，100Hz 分辨率；b2 高半字节固定 0）
      if (parsed->payload.size() != 3) {
        pushResponse(civ::kNak, {});
        return true;
      }
      const auto nib = [](uint8_t b, int which) {
        return which ? (b >> 4) : (b & 0x0F);
      };
      const uint32_t hz =
          nib(parsed->payload[0], 1) * 1000U + nib(parsed->payload[0], 0) * 100U +
          nib(parsed->payload[1], 1) * 100000U +
          nib(parsed->payload[1], 0) * 10000U +
          nib(parsed->payload[2], 0) * 1000000U;
      if (nib(parsed->payload[0], 1) > 9 || nib(parsed->payload[0], 0) > 9 ||
          nib(parsed->payload[1], 1) > 9 || nib(parsed->payload[1], 0) > 9 ||
          (parsed->payload[2] >> 4) != 0 || hz > 9'999'900U) {
        pushResponse(civ::kNak, {});
      } else {
        sim_.offset_hz = hz;
        pushResponse(civ::kAck, {});
      }
      return true;
    }
    case civ::kCmdRit: {
      if (parsed->payload.empty()) return true;
      const uint8_t sub = parsed->payload[0];
      if (parsed->payload.size() == 1) {  // 读
        bool on = sub == 0x02 ? sim_.dtx : sim_.rit;
        const uint8_t p[] = {sub, static_cast<uint8_t>(on ? 1 : 0)};
        pushResponse(civ::kCmdRit, p);
        return true;
      }
      const bool ok = parsed->payload.size() == 2 && parsed->payload[1] <= 1;
      if (ok) {
        if (sub == 0x02) sim_.dtx = parsed->payload[1] != 0;
        else if (sub == 0x01) sim_.rit = parsed->payload[1] != 0;
      }
      pushResponse(ok ? civ::kAck : civ::kNak, {});
      return true;
    }
    case civ::kCmdTone: {  // 0x1B 0x00/0x01 亚音频率；0x02 DTCS 码+极性
      if (parsed->payload.empty()) {
        pushResponse(civ::kNak, {});
        return true;
      }
      const uint8_t sub = parsed->payload[0];
      if (sub > civ::kSubToneDtcs) {
        pushResponse(civ::kNak, {});
        return true;
      }
      if (parsed->payload.size() == 1) {  // 读
        std::vector<uint8_t> p{sub};
        if (sub == civ::kSubToneDtcs) {
          const int code = sim_.dtcs_code;
          p.push_back(static_cast<uint8_t>(((sim_.dtcs_tx_polarity & 1) << 4) |
                                           (sim_.dtcs_rx_polarity & 1)));
          // 与 dtcsCodeFrame 同构：b0 高半字节固定 0 + 百位，b1=十位<<4|个位
          p.push_back(static_cast<uint8_t>((code / 100) % 10));
          p.push_back(static_cast<uint8_t>((((code / 10) % 10) << 4) |
                                           (code % 10)));
        } else {
          const uint16_t deci =
              sub == civ::kSubToneTsql ? sim_.tsql_decihz : sim_.tone_decihz;
          p.push_back(0x00);
          p.push_back(static_cast<uint8_t>(((deci / 1000) << 4) |
                                           ((deci / 100) % 10)));
          p.push_back(static_cast<uint8_t>((((deci / 10) % 10) << 4) |
                                           (deci % 10)));
        }
        pushResponse(civ::kCmdTone, p);
        return true;
      }
      if (parsed->payload.size() != 4) {
        pushResponse(civ::kNak, {});
        return true;
      }
      if (sub == civ::kSubToneDtcs) {
        const int pol = parsed->payload[1];
        const int rx_pol = pol & 0x0F, tx_pol = pol >> 4;
        const int d1 = parsed->payload[2] >> 4, d2 = parsed->payload[2] & 0x0F;
        const int d3 = parsed->payload[3] >> 4, d4 = parsed->payload[3] & 0x0F;
        if (rx_pol > 1 || tx_pol > 1 || d1 != 0 || d2 > 7 || d3 > 7 || d4 > 7) {
          pushResponse(civ::kNak, {});
          return true;
        }
        sim_.dtcs_code = d2 * 100 + d3 * 10 + d4;
        sim_.dtcs_rx_polarity = rx_pol;
        sim_.dtcs_tx_polarity = tx_pol;
        pushResponse(civ::kAck, {});
        return true;
      }
      if (parsed->payload[1] != 0x00) {
        pushResponse(civ::kNak, {});
        return true;
      }
      const int d1 = parsed->payload[2] >> 4, d2 = parsed->payload[2] & 0x0F;
      const int d3 = parsed->payload[3] >> 4, d4 = parsed->payload[3] & 0x0F;
      if (d1 > 2 || d2 > 9 || d3 > 9 || d4 > 9) {
        pushResponse(civ::kNak, {});
        return true;
      }
      const auto deci = static_cast<uint16_t>(d1 * 1000 + d2 * 100 + d3 * 10 + d4);
      (sub == civ::kSubToneTsql ? sim_.tsql_decihz : sim_.tone_decihz) = deci;
      pushResponse(civ::kAck, {});
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
          case civ::kSub16BreakIn: val = sim_.break_in ? 1 : 0; break;
          case civ::kSub16ToneSquelch:
            val = static_cast<uint8_t>(sim_.tone_squelch);
            break;
          case civ::kSub16RepeaterTone: val = sim_.repeater_tone ? 1 : 0; break;
          case civ::kSub16RepeaterTsql: val = sim_.tsql ? 1 : 0; break;
          case civ::kSub16Dtcs: val = sim_.dtcs_on ? 1 : 0; break;
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
        case civ::kSub16BreakIn: ok = val <= 1; if (ok) sim_.break_in = val != 0; break;
        case civ::kSub16ToneSquelch:
          ok = civ::toneSquelchFromCode(val).has_value();
          if (ok) sim_.tone_squelch = val;
          break;
        case civ::kSub16RepeaterTone: ok = val <= 1; if (ok) sim_.repeater_tone = val != 0; break;
        case civ::kSub16RepeaterTsql: ok = val <= 1; if (ok) sim_.tsql = val != 0; break;
        case civ::kSub16Dtcs: ok = val <= 1; if (ok) sim_.dtcs_on = val != 0; break;
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
    case civ::kCmdVfoFreq: {  // 0x25：读/写指定 VFO 频率
      if (parsed->payload.empty() || parsed->payload[0] > 1) {
        pushResponse(civ::kNak, {});
        return true;
      }
      const bool unselected = parsed->payload[0] == 1;
      if (parsed->payload.size() == 1) {
        const uint64_t hz = unselected ? sim_.sub_freq_hz : sim_.freq_hz;
        std::vector<uint8_t> p{parsed->payload[0]};
        for (uint8_t b : civ::freqToBcd5(hz).value_or(std::array<uint8_t, 5>{}))
          p.push_back(b);
        pushResponse(civ::kCmdVfoFreq, p);
        return true;
      }
      if (parsed->payload.size() != 6) {
        pushResponse(civ::kNak, {});
        return true;
      }
      auto hz = civ::bcdToFreq(std::span<const uint8_t>(parsed->payload).subspan(1, 5));
      if (!hz || !isRxAllowed(*hz)) {
        pushResponse(civ::kNak, {});
      } else {
        (unselected ? sim_.sub_freq_hz : sim_.freq_hz) = *hz;
        pushResponse(civ::kAck, {});
      }
      return true;
    }
    case civ::kCmdVfoMode: {  // 0x26：读/写 [sub, mode, data, filter]
      if (parsed->payload.empty() || parsed->payload[0] > 1) {
        pushResponse(civ::kNak, {});
        return true;
      }
      const bool unselected = parsed->payload[0] == 1;
      if (parsed->payload.size() == 1) {
        const uint8_t p[] = {
            parsed->payload[0],
            static_cast<uint8_t>(unselected ? sim_.sub_mode : sim_.mode),
            static_cast<uint8_t>(unselected ? sim_.sub_data_mode : 0),
            static_cast<uint8_t>(unselected ? sim_.sub_filter : sim_.filter)};
        pushResponse(civ::kCmdVfoMode, p);
        return true;
      }
      if (parsed->payload.size() != 4 || !civ::modeFromCode(parsed->payload[1])) {
        pushResponse(civ::kNak, {});
        return true;
      }
      if (unselected) {
        sim_.sub_mode = static_cast<civ::Mode>(parsed->payload[1]);
        sim_.sub_data_mode = parsed->payload[2] & 0x01;
        sim_.sub_filter = parsed->payload[3];
      } else {
        sim_.mode = static_cast<civ::Mode>(parsed->payload[1]);
        sim_.filter = parsed->payload[3];
      }
      pushResponse(civ::kAck, {});
      return true;
    }
    case civ::kCmdVfoSelect: {  // 0x07
      if (parsed->payload.size() != 1 || parsed->payload[0] > 1) {
        pushResponse(civ::kNak, {});
      } else {
        sim_.selected_sub = parsed->payload[0] == 1;
        pushResponse(civ::kAck, {});
      }
      return true;
    }
    case civ::kCmdSwapVfo: {  // 0xB0
      std::swap(sim_.freq_hz, sim_.sub_freq_hz);
      std::swap(sim_.mode, sim_.sub_mode);
      std::swap(sim_.filter, sim_.sub_filter);
      pushResponse(civ::kAck, {});
      return true;
    }
    case civ::kCmdEqualVfo: {  // 0xA0：把当前 VFO 复制到另一 VFO
      if (sim_.selected_sub) {
        sim_.freq_hz = sim_.sub_freq_hz;
        sim_.mode = sim_.sub_mode;
        sim_.filter = sim_.sub_filter;
      } else {
        sim_.sub_freq_hz = sim_.freq_hz;
        sim_.sub_mode = sim_.mode;
        sim_.sub_filter = sim_.filter;
      }
      pushResponse(civ::kAck, {});
      return true;
    }
    case civ::kCmdLevel14: {  // 0x14 电平族（2B BCD MSB-first）
      if (parsed->payload.empty()) {
        pushResponse(civ::kNak, {});
        return true;
      }
      int* target = nullptr;
      switch (parsed->payload[0]) {
        case civ::kSub14CwPitch: target = &sim_.cw_pitch_level; break;
        case civ::kSub14KeySpeed: target = &sim_.key_speed_level; break;
        case civ::kSub14BreakInDelay: target = &sim_.break_in_delay_level; break;
        default: pushResponse(civ::kNak, {}); return true;
      }
      if (parsed->payload.size() == 1) {
        std::vector<uint8_t> p{parsed->payload[0]};
        for (uint8_t b : bcdMsb(*target, 2)) p.push_back(b);
        pushResponse(civ::kCmdLevel14, p);
        return true;
      }
      if (parsed->payload.size() != 3) {
        pushResponse(civ::kNak, {});
        return true;
      }
      const int v = bcdMsbToInt(std::span<const uint8_t>(parsed->payload).subspan(1, 2));
      if (v < 0 || v > 255) {
        pushResponse(civ::kNak, {});
      } else {
        *target = v;
        pushResponse(civ::kAck, {});
      }
      return true;
    }
    case civ::kCmdCw: {  // 0x17：ASCII 文本（≤30B/帧）/ 0xFF 停止
      if (parsed->payload.empty()) {
        pushResponse(civ::kNak, {});
      } else if (parsed->payload[0] == civ::kCwStop) {
        sim_.cw_text.clear();
        sim_.cw_sending = false;
        pushResponse(civ::kAck, {});
      } else {
        sim_.cw_text.append(reinterpret_cast<const char*>(parsed->payload.data()),
                            parsed->payload.size());
        sim_.cw_sending = true;
        pushResponse(civ::kAck, {});
      }
      return true;
    }
    case civ::kCmdMemoryMode: {  // 0x08 / 0x08 A0：选记忆信道 / 记忆组
      if (parsed->payload.size() == 2) {
        const int ch = bcdMsbToInt(parsed->payload);
        if (ch < 0) {
          pushResponse(civ::kNak, {});
        } else {
          sim_.memory_channel = ch;
          pushResponse(civ::kAck, {});
        }
        return true;
      }
      if (parsed->payload.size() == 3 &&
          parsed->payload[0] == civ::kSubMemoryGroup) {
        const int group =
            bcdMsbToInt(std::span<const uint8_t>(parsed->payload).subspan(1, 2));
        if (group < 0) {
          pushResponse(civ::kNak, {});
        } else {
          sim_.memory_group = group;
          pushResponse(civ::kAck, {});
        }
        return true;
      }
      pushResponse(civ::kNak, {});
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
