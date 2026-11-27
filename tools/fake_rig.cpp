// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tools/fake_rig.cpp — 假电台 LAN 化台架工具（WO-275 L1）
//
// 限定词（WO-275 §4.3 红线）：验「本仓客户端 vs 已归档 IC-705 fixtures 一致性」。
// 不验真机时钟漂移、真实 50003 抖动丢包、射频 PTT 尾、实际延迟与音质保真。
// 经它打勾的项在验收清单里必须写「LAN 假电台验通」，与「真机验通」分开记。
//
// 三信道应答 = tests/fake_rsba1_server.cpp（与 tests/fixtures/*.hex 逐字节一致，
// IC-705 真机验证 2026-08-18）。固定端口 ctrl=n/serial=n+1/audio=n+2（默认 50001 起）。
// 安全闸照抄 bus_server.cpp:129：非环回必须 --i-know-this-is-lan，否则退出不监听。
//
// --selftest：内部真客户端（Rsba1UdpTransport 连 127.0.0.1）握手→CI-V 往返自证。
// nominal / audio-* 场景预期 PASS；fault 场景下 FAIL = 场景生效的证据。
//
// 场景（--scenario）：nominal | silence-after-N | slow-handshake | nak | auth-deny
//                     | audio-rx-only | audio-loopback | flap

#include <QCoreApplication>
#include <QHostAddress>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "core/civ.h"
#include "core/rsba1_proto.h"
#include "core/transport_udp.h"
#include "fake_rsba1_server.h"

using radio::core::Rsba1UdpTransport;

namespace {

constexpr uint16_t kDefaultBasePort = 50001;
constexpr int kPollMs = 20;
constexpr int kSelftestTimeoutMs = 15000;

struct Args {
  std::string bind = "127.0.0.1";
  uint16_t base_port = kDefaultBasePort;
  std::string scenario = "nominal";
  int silence_after = 10;
  int handshake_delay_ms = 5000;
  int flap_down_after = 8;
  int flap_up_after = 3;
  int32_t login_result = 1;
  int duration = 0;  // 0 = 直到 Ctrl+C
  bool lan_flag = false;
  bool selftest = false;
};

[[noreturn]] void usage() {
  std::cerr
      << "用法: fake_rig [选项]\n"
      << "  --bind <addr>            绑定地址（默认 127.0.0.1）\n"
      << "  --base-port <n>          ctrl=n, serial=n+1, audio=n+2（默认 50001）\n"
      << "  --scenario <name>        nominal|silence-after-N|slow-handshake|nak|"
         "auth-deny|audio-rx-only|audio-loopback|flap（默认 nominal）\n"
      << "  --silence-after <sec>    silence-after-N 的 N（默认 10）\n"
      << "  --handshake-delay <ms>   slow-handshake 拖时（默认 5000）\n"
      << "  --flap-down-after <sec>  flap：建链后几秒断链（默认 8）\n"
      << "  --flap-up-after <sec>    flap：断链后几秒重绑（默认 3）\n"
      << "  --login-result <n>       auth-deny 的登录拒绝码（默认 1）\n"
      << "  --duration <sec>         服务时长（默认 0 = 直到 Ctrl+C）\n"
      << "  --i-know-this-is-lan     非环回绑定的显式旗标（缺它直接退出）\n"
      << "  --selftest               内部真客户端握手→CI-V 往返自证\n";
  std::exit(2);
}

Args parseArgs(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    const std::string s = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) usage();
      return argv[++i];
    };
    if (s == "--bind") {
      a.bind = next();
    } else if (s == "--base-port") {
      a.base_port = static_cast<uint16_t>(std::stoi(next()));
    } else if (s == "--scenario") {
      a.scenario = next();
    } else if (s == "--silence-after") {
      a.silence_after = std::stoi(next());
    } else if (s == "--handshake-delay") {
      a.handshake_delay_ms = std::stoi(next());
    } else if (s == "--flap-down-after") {
      a.flap_down_after = std::stoi(next());
    } else if (s == "--flap-up-after") {
      a.flap_up_after = std::stoi(next());
    } else if (s == "--login-result") {
      a.login_result = std::stoi(next());
    } else if (s == "--duration") {
      a.duration = std::stoi(next());
    } else if (s == "--i-know-this-is-lan") {
      a.lan_flag = true;
    } else if (s == "--selftest") {
      a.selftest = true;
    } else {
      usage();
    }
  }
  return a;
}

// 48kHz·20ms = 960 样本 = 1920B → part1(1364B) + part2(556B)
struct ToneFrame {
  std::vector<uint8_t> part1;
  std::vector<uint8_t> part2;
};

class ToneSource {
 public:
  explicit ToneSource(int freq_hz = 440) : freq_hz_(freq_hz) {}

  ToneFrame next() {
    ToneFrame f;
    f.part1.resize(1364);
    f.part2.resize(556);
    auto* p1 = reinterpret_cast<int16_t*>(f.part1.data());
    auto* p2 = reinterpret_cast<int16_t*>(f.part2.data());
    for (int i = 0; i < 960; ++i) {
      const double s = 8000.0 * std::sin(2.0 * 3.14159265358979323846 *
                                         freq_hz_ * phase_);
      const auto v = static_cast<int16_t>(s);
      if (i < 682) {
        p1[i] = v;
      } else {
        p2[i - 682] = v;
      }
    }
    phase_ += 960.0 / 48000.0;
    return f;
  }

 private:
  int freq_hz_ = 440;
  double phase_ = 0.0;
};

bool runSelftest(FakeRsba1Server& srv, bool expect_nak, std::string* msg) {
  Rsba1UdpTransport::Config cfg;
  cfg.host = "127.0.0.1";  // 自证客户端永远走环回（服务端绑 0.0.0.0 也能通）
  cfg.username = "bench";
  cfg.password = "bench";
  cfg.cmd_port = srv.ctrlPort();
  cfg.ser_port = srv.serialPort();
  cfg.aud_port = srv.audioPort();
  cfg.handshake_timeout_ms = kSelftestTimeoutMs;
  Rsba1UdpTransport t(std::move(cfg));
  std::string terr;
  if (!t.open(&terr)) {
    if (msg) *msg = "客户端握手失败: " + terr + " | lastLog: " + t.lastLog();
    return false;
  }

  // CI-V 往返：上行读频率 → 服务器见帧 → 下行回应答 → 客户端回调见帧
  std::vector<uint8_t> delivered;
  t.setFrameCallback([&](std::span<const uint8_t> f) {
    delivered.assign(f.begin(), f.end());
  });
  const std::vector<uint8_t> cmd = {0xFE, 0xFE, 0xE0, 0x03, 0xFD};  // 读频率
  if (!t.send(cmd)) {
    if (msg) *msg = "CI-V 上行发送失败";
    t.close();
    return false;
  }
  bool saw_uplink = false;
  const auto d1 = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(kSelftestTimeoutMs);
  while (std::chrono::steady_clock::now() < d1) {
    t.poll();
    for (auto& f : srv.clientCivFrames())
      if (f == cmd) saw_uplink = true;
    if (saw_uplink) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    if (saw_uplink) break;
  }
  if (!saw_uplink) {
    if (msg) *msg = "服务器未见 CI-V 上行 | lastLog: " + t.lastLog();
    t.close();
    return false;
  }
  const std::vector<uint8_t> resp = {0xFE, 0xFE, 0xE0, 0x03,
                                     0x00, 0x50, 0x14, 0x27, 0x00, 0xFD};
  if (!expect_nak) {
    srv.sendCiv(resp);
  }
  bool got_downlink = false;
  const auto d2 = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(kSelftestTimeoutMs);
  while (std::chrono::steady_clock::now() < d2) {
    t.poll();
    if (!delivered.empty()) {
      got_downlink = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
  if (!got_downlink) {
    if (msg) *msg = "客户端未见下行应答 | lastLog: " + t.lastLog();
    t.close();
    return false;
  }
  bool ok = false;
  if (expect_nak) {
    // nak 场景：场景循环会把 NAK（FE FE dst src FA cmd FD）回给客户端
    ok = delivered.size() >= 5 && delivered[4] == 0xFA;
    if (!ok && msg) *msg = "未收到 NAK 或字节不符";
  } else {
    ok = delivered == resp;
    if (!ok && msg) *msg = "下行应答字节不一致";
  }
  t.close();
  return ok;
}

int runTool(const Args& a) {
  // ---- 安全闸（bus_server.cpp:129 先例）----
  QHostAddress bind_addr;
  if (!bind_addr.setAddress(QString::fromStdString(a.bind)) ||
      bind_addr.protocol() == QAbstractSocket::UnknownNetworkLayerProtocol) {
    std::cerr << "[拒绝] 无法解析绑定地址: " << a.bind << "\n";
    return 2;
  }
  const bool lan = !bind_addr.isLoopback();
  if (lan && !a.lan_flag) {
    std::cerr << "[拒绝] 非环回绑定（" << a.bind
              << "）必须显式 --i-know-this-is-lan\n"
                 "本进程假扮电台，不做任何鉴权，只允许在受控局域网跑。\n";
    return 2;
  }
  if (lan) {
    std::cout << "\x1b[31;1m[警告] 本进程假扮电台，不做任何鉴权，只允许在受控局域网跑，"
                 "产品代码不得依赖它。\x1b[0m\n";
  }

  // ---- 场景配置 ----
  FakeRsba1Server srv;
  srv.bind_address = bind_addr;
  srv.ctrl_port = a.base_port;
  srv.serial_port = a.base_port + 1;
  srv.audio_port = a.base_port + 2;
  if (a.scenario == "slow-handshake") {
    srv.handshake_delay_ms = a.handshake_delay_ms;
  } else if (a.scenario == "auth-deny" && a.login_result != 0) {
    srv.login_result_code = a.login_result;
  } else if (a.scenario != "nominal" && a.scenario != "silence-after-N" &&
             a.scenario != "nak" && a.scenario != "audio-rx-only" &&
             a.scenario != "audio-loopback" && a.scenario != "flap") {
    std::cerr << "[拒绝] 未知场景: " << a.scenario << "\n";
    return 2;
  }

  std::string err;
  if (!srv.start(&err)) {
    std::cerr << "[错误] 假电台启动失败: " << err << "\n";
    return 1;
  }

  // ---- 横幅 ----
  std::cout << "==== fake_rig ====\n"
            << "bind      : " << a.bind << "\n"
            << "scenario  : " << a.scenario << "\n"
            << "ports     : " << srv.ctrlPort() << "/" << srv.serialPort() << "/"
            << srv.audioPort() << " (ctrl/serial/audio)\n"
            << "停止      : Ctrl+C（或 --duration 秒后自动退出）" << std::endl;

  // ---- 主循环（线程化：与 selftest 并发跑场景事件）----
  size_t uplinks = 0;
  uint64_t rig_hz = 14270000;  // 假电台的「当前频率」，写频会改它
  bool flap_failed = false;
  std::thread loop_thr([&] {
  ToneSource tone;
  const auto t0 = std::chrono::steady_clock::now();
  int audio_seq = 1;
  bool silence_injected = false;
  bool flapped = false;
  bool loopback_muted = false;
  const bool has_deadline = a.duration > 0;
  auto deadline = t0 + std::chrono::seconds(a.duration > 0 ? a.duration : 0);

  while (!has_deadline || std::chrono::steady_clock::now() < deadline) {
    const int elapsed = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - t0)
            .count());

    // silence-after-N：N 秒后关 socket = 对端只见静默；进程保持存活
    if (a.scenario == "silence-after-N" && !silence_injected &&
        elapsed >= a.silence_after) {
      srv.stop();
      std::cout << "[silence] 已注入：三信道全部静默（进程保持存活）" << std::endl;
      silence_injected = true;
    }
    // flap：建链且到时 → 断链 → M 秒后同端口重绑
    if (a.scenario == "flap" && !flapped && srv.serialOpened() &&
        elapsed >= a.flap_down_after) {
      std::cout << "[flap] 注入断链（stop）" << std::endl;
      srv.stop();
      std::this_thread::sleep_for(std::chrono::seconds(a.flap_up_after));
      std::string rerr;
      if (!srv.start(&rerr)) {
        std::cerr << "[flap] 重绑失败: " << rerr << "\n";
        flap_failed = true;
        break;
      }
      std::cout << "[flap] 已同端口重绑，等待客户端恢复" << std::endl;
      flapped = true;
    }
    // audio-rx-only：每 20ms 喂一帧（part1+part2 同 seq）
    if (a.scenario == "audio-rx-only" && srv.serialOpened()) {
      const ToneFrame f = tone.next();
      srv.sendAudio(f.part1, static_cast<uint16_t>(audio_seq & 0xFFFF));
      srv.sendAudio(f.part2, static_cast<uint16_t>(audio_seq & 0xFFFF));
      ++audio_seq;
    }
    // audio-loopback：上行回喂；收到上行后停发合成下行（半双工静音）
    if (a.scenario == "audio-loopback" && srv.serialOpened()) {
      auto up = srv.drainAudioFrames();
      if (!up.empty()) {
        for (auto& f : up) {
          srv.sendAudio(std::span<const uint8_t>(f).first(1364),
                        static_cast<uint16_t>(audio_seq & 0xFFFF));
          srv.sendAudio(std::span<const uint8_t>(f).last(556),
                        static_cast<uint16_t>(audio_seq & 0xFFFF));
          ++audio_seq;
        }
        if (!loopback_muted) {
          std::cout << "[loopback] 见上行音频，合成下行已静音（半双工）" << std::endl;
          loopback_muted = true;
        }
      }
      if (!loopback_muted) {
        const ToneFrame f = tone.next();
        srv.sendAudio(f.part1, static_cast<uint16_t>(audio_seq & 0xFFFF));
        srv.sendAudio(f.part2, static_cast<uint16_t>(audio_seq & 0xFFFF));
        ++audio_seq;
      }
    }
    // 最小电台模型（nominal 及其它非 nak 场景）：按请求回预置应答。
    // 没有它，客户端会一直轮询却永远拿不到回帧——A-1.5 的「状态回读」在
    // 真网络上必然失败（环回用例与 selftest 都是自己 sendCiv 塞一条，不是服务端自动回）。
    if (a.scenario != "nak") {
      for (auto& f : srv.drainCivFrames()) {
        ++uplinks;
        if (f.size() < 5) continue;
        // 应答用 tests/fixtures 的真机抓包原文（IC-705，2026-08-18）：
        // 会话层解析器认的是这套字节，自己拼的「看起来像」的帧会被静默丢。
        std::vector<uint8_t> r;
        const std::vector<uint8_t> body(f.begin() + 5, f.end() - 1);
        switch (f[4]) {
          case 0x03: {  // 读频率 → 当前 rig_hz
            const auto bcd = radio::core::civ::freqToBcd5(rig_hz);
            if (!bcd) break;
            r = {0xFE, 0xFE, 0x00, 0xA4, 0x03};
            r.insert(r.end(), bcd->begin(), bcd->end());
            r.push_back(0xFD);
            break;
          }
          case 0x05: {  // 写频率：收下，回 ACK(FB)
            if (auto hz = radio::core::civ::bcdToFreq(body)) rig_hz = *hz;
            r = {0xFE, 0xFE, 0xE0, 0xA4, 0xFB, 0xFD};
            break;
          }
          case 0x04:  // 读模式 → FM / filter 1
            r = {0xFE, 0xFE, 0x00, 0xA4, 0x04, 0x05, 0x01, 0xFD};
            break;
          case 0x1C:  // 读 PTT → RX
            r = {0xFE, 0xFE, 0x00, 0xA4, 0x1C, 0x00, 0x00, 0xFD};
            break;
          case 0x1A:  // 读 S 表（子命令 0x03）→ S9
            if (f.size() > 5 && f[5] == 0x03)
              r = {0xFE, 0xFE, 0x00, 0xA4, 0x1A, 0x03, 0x78, 0xFD};
            break;
          default:
            break;
        }
        if (!r.empty()) srv.sendCiv(r);
      }
    }
    // nak：镜像 dst/src 回 NAK，payload 带被拒命令字节（session.cpp:253 语义）
    if (a.scenario == "nak") {
      for (auto& f : srv.drainCivFrames()) {
        const std::vector<uint8_t> nak = {
            0xFE, 0xFE,
            static_cast<uint8_t>(f.size() > 2 ? f[2] : 0xE0),
            static_cast<uint8_t>(f.size() > 3 ? f[3] : 0x03),
            0xFA,
            static_cast<uint8_t>(f.size() < 5 ? 0x03 : f[4]),
            0xFD};
        srv.sendCiv(nak);
        ++uplinks;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
  }
  });  // 场景循环线程

  // ---- selftest（与场景循环并发）----
  bool selftest_ok = true;
  const bool should_pass = (a.scenario != "nak" && a.scenario != "auth-deny");
  if (a.selftest) {
    std::string msg;
    selftest_ok = runSelftest(srv, a.scenario == "nak", &msg);
    std::cout << (selftest_ok ? "[selftest] PASS: 握手→认证→建流→CI-V 往返全通\n"
                              : "[selftest] FAIL: " + msg + "\n")
              << std::flush;
  }
  loop_thr.join();
  if (flap_failed) return 1;
  if (a.selftest && !selftest_ok && should_pass) return 3;

  // ---- 退出小结 ----
  std::cout << "==== fake_rig 退出小结 ====\n"
            << "scenario    : " << a.scenario << "\n"
            << "CI-V 上行帧 : " << uplinks + srv.clientCivFrames().size() << " 条\n"
            << "serial open : " << (srv.serialOpened() ? "yes" : "no") << "\n"
            << "disconnects : " << srv.disconnectCount() << "\n";
  srv.stop();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const Args a = parseArgs(argc, argv);
  return runTool(a);
}
