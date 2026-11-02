// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "app/session.h"
#include "core/civ.h"
#include "core/transport_mock.h"
#include "qt_test_app.h"

namespace {

class RecordingTransport final : public radio::core::ICivTransport {
 public:
  bool open(std::string*) override {
    open_ = true;
    return true;
  }
  void close() override { open_ = false; }
  bool isOpen() const override { return open_; }
  bool send(std::span<const uint8_t> frame) override {
    sent.emplace_back(frame.begin(), frame.end());
    return true;
  }

  std::vector<std::vector<uint8_t>> sent;

 private:
  bool open_ = false;
};

struct SessionFixture {
  SessionFixture() {
    qtTestApp();
    transport = std::make_shared<RecordingTransport>();
    session = std::make_unique<radio::app::RadioSession>(transport);
    session->start();
    transport->sent.clear();
  }

  void setFrequency(uint64_t hz) {
    auto state = session->store().snapshot();
    state.freq_hz = hz;
    session->store().update(state);
  }

  std::shared_ptr<RecordingTransport> transport;
  std::unique_ptr<radio::app::RadioSession> session;
};

std::vector<uint8_t> ptt(bool on) {
  return radio::core::civ::pttFrame(on);
}

// 闭环会话：MockTransport 会真的回读，用来验证 state ↔ 命令的一致性
struct MockSessionFixture {
  explicit MockSessionFixture(radio::core::MockTransport::SimConfig cfg = {}) {
    qtTestApp();
    transport = std::make_shared<radio::core::MockTransport>(cfg);
    session = std::make_unique<radio::app::RadioSession>(transport);
    session->start();  // 首轮 poll 把 mock 状态灌进 store
  }

  std::shared_ptr<radio::core::MockTransport> transport;
  std::unique_ptr<radio::app::RadioSession> session;
};

}  // namespace

TEST_CASE("session scripted tx keys once and always returns to rx", "[session][ptt]") {
  SessionFixture f;
  f.setFrequency(14'270'000);

  REQUIRE(f.session->beginScriptedTx());
  REQUIRE(f.session->gate().phase() == radio::core::PttPhase::Tx);
  REQUIRE(f.transport->sent == std::vector<std::vector<uint8_t>>{ptt(true)});

  f.session->endScriptedTx();
  REQUIRE(f.session->gate().phase() == radio::core::PttPhase::Rx);
  REQUIRE(f.transport->sent ==
          std::vector<std::vector<uint8_t>>{ptt(true), ptt(false)});

  f.session->endScriptedTx();
  REQUIRE(f.transport->sent.size() == 2);
}

TEST_CASE("session scripted tx rejects non-amateur frequency without keying", "[session][ptt]") {
  SessionFixture f;
  f.setFrequency(100'000'000);

  REQUIRE_FALSE(f.session->beginScriptedTx());
  REQUIRE(f.session->gate().phase() == radio::core::PttPhase::Rx);
  REQUIRE(f.transport->sent.empty());
}

TEST_CASE("session manual ptt keeps two-step confirmation", "[session][ptt]") {
  SessionFixture f;
  f.setFrequency(14'270'000);

  f.session->pttButtonPressed();
  REQUIRE(f.session->gate().phase() == radio::core::PttPhase::ConfirmPending);
  REQUIRE(f.transport->sent.empty());

  f.session->pttButtonPressed();
  REQUIRE(f.transport->sent == std::vector<std::vector<uint8_t>>{ptt(true)});
  f.session->pttButtonPressed();
  REQUIRE(f.session->gate().phase() == radio::core::PttPhase::Rx);
  REQUIRE(f.transport->sent ==
          std::vector<std::vector<uint8_t>>{ptt(true), ptt(false)});
}

TEST_CASE("session scripted tx does not rekey an active transmission", "[session][ptt]") {
  SessionFixture f;
  f.setFrequency(14'270'000);
  f.session->pttButtonPressed();
  f.session->pttButtonPressed();
  f.transport->sent.clear();

  REQUIRE(f.session->beginScriptedTx());
  REQUIRE(f.transport->sent.empty());
  f.session->endScriptedTx();
  REQUIRE(f.transport->sent == std::vector<std::vector<uint8_t>>{ptt(false)});
}

TEST_CASE("session cw text keys once and stops cleanly", "[session][cw]") {
  MockSessionFixture f;
  REQUIRE(f.session->sendCwText("cq de k1abc", 20));
  REQUIRE(f.session->gate().phase() == radio::core::PttPhase::Tx);
  CHECK(f.transport->sim().ptt);
  CHECK(f.transport->sim().cw_text == "CQ DE K1ABC");

  f.session->stopCw();
  REQUIRE(f.session->gate().phase() == radio::core::PttPhase::Rx);
  CHECK_FALSE(f.transport->sim().ptt);
  CHECK_FALSE(f.transport->sim().cw_sending);
  CHECK(f.transport->sim().cw_text.empty());
}

TEST_CASE("session cw refused outside amateur bands without keying", "[session][cw]") {
  radio::core::MockTransport::SimConfig cfg;
  cfg.freq_hz = 98'000'000;  // 广播段：可听不可发
  MockSessionFixture f(cfg);

  REQUIRE_FALSE(f.session->sendCwText("CQ", 20));
  CHECK(f.session->gate().phase() == radio::core::PttPhase::Rx);
  CHECK_FALSE(f.transport->sim().ptt);
  CHECK(f.transport->sim().cw_text.empty());
}

TEST_CASE("session cw settings roundtrip", "[session][cw]") {
  MockSessionFixture f;
  using namespace radio::core;

  f.session->setKeySpeed(25);
  CHECK(f.session->store().snapshot().cw_key_speed_level ==
        civ::wpmToKeySpeedLevel(25));
  f.session->setCwPitch(700);
  CHECK(f.session->store().snapshot().cw_pitch_level == civ::hzToCwPitchLevel(700));
  f.session->setBreakIn(true);
  CHECK(f.session->store().snapshot().break_in);
  f.session->setBreakInDelay(120);
  CHECK(f.session->store().snapshot().break_in_delay_level == 120);
}

TEST_CASE("session vfo switching refused while tx", "[session][vfo]") {
  MockSessionFixture f;
  REQUIRE(f.session->beginScriptedTx());
  CHECK_FALSE(f.session->selectVfo(true));
  CHECK_FALSE(f.session->swapVfo());
  CHECK_FALSE(f.session->equalizeVfo());
  f.session->endScriptedTx();

  CHECK(f.session->selectVfo(true));
  CHECK(f.session->store().snapshot().selected_sub);
}

TEST_CASE("session sub band tuning and mode update the other vfo", "[session][vfo]") {
  MockSessionFixture f;
  using radio::core::civ::Mode;

  REQUIRE(f.session->tuneSubTo(7'030'000));
  auto s = f.session->store().snapshot();
  CHECK(s.freq_hz == 14'270'000ULL);
  CHECK(s.sub_freq_hz == 7'030'000ULL);

  REQUIRE(f.session->setSubMode(Mode::CW, 0, 2));
  s = f.session->store().snapshot();
  CHECK(s.sub_mode == Mode::CW);
  CHECK(s.sub_filter == 2);
  CHECK(s.mode == Mode::USB);

  CHECK(f.session->swapVfo());
  s = f.session->store().snapshot();
  CHECK(s.freq_hz == 7'030'000ULL);
  CHECK(s.sub_freq_hz == 14'270'000ULL);
}

TEST_CASE("session menu and memory reads update state", "[session][menu][memory]") {
  MockSessionFixture f;
  using namespace radio::core;

  const uint8_t v[] = {0x20};
  f.session->writeMenuItem(0x03, 0x59, v);
  auto s = f.session->store().snapshot();
  const uint16_t key = static_cast<uint16_t>((0x03 << 8) | 0x59);
  REQUIRE(s.menu_values.count(key) == 1);
  CHECK(s.menu_values.at(key) == std::vector<uint8_t>{0x20});

  civ::MemoryChannel m;
  m.channel = 5;
  m.freq_hz = 7'050'000;
  m.mode = civ::Mode::LSB;
  m.filter = 1;
  m.name = "TEST";
  f.session->writeMemoryChannel(m);
  s = f.session->store().snapshot();
  REQUIRE(s.memory_channel.has_value());
  CHECK(*s.memory_channel == m);
  CHECK(s.memory_index == 5);

  f.session->selectMemoryGroup(3);
  CHECK(f.session->store().snapshot().memory_group == 3);
  f.session->selectMemoryChannel(7);
  CHECK(f.session->store().snapshot().memory_index == 7);
}

TEST_CASE("session offset roundtrip and tx frequency", "[session][offset]") {
  MockSessionFixture f;
  using namespace radio::core;

  REQUIRE(f.session->setOffset(600'000));
  auto s = f.session->store().snapshot();
  CHECK(s.offset_hz == 600'000U);

  // 100Hz 分辨率与 9.9999MHz 上限
  CHECK_FALSE(f.session->setOffset(600'001));
  CHECK_FALSE(f.session->setOffset(10'000'000));
  CHECK(f.session->store().snapshot().offset_hz == 600'000U);

  f.session->setDuplex(0x11);
  s = f.session->store().snapshot();
  CHECK(s.duplex_raw == 0x11);
  CHECK(radio::core::effectiveTxHz(s) == s.freq_hz - 600'000);  // DUP−

  f.session->setDuplex(0x12);
  s = f.session->store().snapshot();
  CHECK(radio::core::effectiveTxHz(s) == s.freq_hz + 600'000);  // DUP+

  f.session->setDuplex(0x10);
  s = f.session->store().snapshot();
  CHECK(radio::core::effectiveTxHz(s) == s.freq_hz);  // simplex：收发同频
}

TEST_CASE("session ctcss and dtcs settings roundtrip", "[session][tone]") {
  MockSessionFixture f;
  using namespace radio::core;

  f.session->setToneSquelch(civ::ToneSquelch::Tsql);
  f.session->setToneFreq(885, false);   // 88.5Hz 中继音
  f.session->setToneFreq(1000, true);   // 100.0Hz TSQL
  f.session->setRepeaterTone(true);
  f.session->setRepeaterTsql(true);
  f.session->setDtcsFunction(true);

  civ::DtcsInfo d;
  d.code = 23;
  d.rx_polarity = 1;
  REQUIRE(f.session->setDtcsCode(d));
  CHECK_FALSE(f.session->setDtcsCode(civ::DtcsInfo{778, 0, 0}));  // 超三位八进制
  CHECK_FALSE(f.session->setToneFreq(3000, false));               // > 299.9Hz

  const auto s = f.session->store().snapshot();
  CHECK(s.tone_squelch == static_cast<int>(civ::ToneSquelch::Tsql));
  CHECK(s.tone_decihz == 885);
  CHECK(s.tsql_decihz == 1000);
  CHECK(s.repeater_tone);
  CHECK(s.tsql);
  CHECK(s.dtcs_on);
  CHECK(s.dtcs_code == 23);
  CHECK(s.dtcs_rx_polarity == 1);
  CHECK(s.dtcs_tx_polarity == 0);
}

namespace {

// 能自己决定「什么时候回一帧、回什么」的传输：链路活性只能由回帧证明，
// 所以测它必须有一个可以装死、也可以开口的对端。
class ScriptedTransport final : public radio::core::ICivTransport {
 public:
  bool open(std::string*) override { return open_ = true; }
  void close() override { open_ = false; }
  bool isOpen() const override { return open_; }
  bool send(std::span<const uint8_t>) override { return open_; }

  void reply(std::vector<uint8_t> frame) { emitFrame(frame); }

 private:
  bool open_ = false;
};

// 活性判定挂在 100 ms 的 tick 上，所以要转事件循环；不为此引 Qt6::Test，
// 用仓库里其它用例同款的 waitUntil。
bool waitUntil(const std::function<bool()>& pred, int timeout_ms = 1500) {
  QElapsedTimer timer;
  timer.start();
  while (!pred() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(2);
  }
  return pred();
}

}  // namespace

TEST_CASE("session: liveness comes from replies and silence is reported honestly",
          "[session][link]") {
  qtTestApp();
  auto t = std::make_shared<ScriptedTransport>();
  radio::app::RadioSession session(t);
  // 活性判定挂在 100 ms 的 tick 上：阈值必须明显大于 tick 周期，否则「评估那一刻
  // 的帧龄」本身就已超阈值，在线永远翻不真。CI 的 Windows runner 把这个时序错误
  // 抓了出来（本地 0.47 s 通过是撞运气）。这里留 5 倍余量。
  session.setLinkTimeoutMs(500);
  std::vector<int> flips;  // 1=恢复 0=失联
  QObject::connect(&session, &radio::app::RadioSession::linkChanged,
                   [&flips](bool alive) { flips.push_back(alive ? 1 : 0); });

  session.start();
  // 一个回帧都没有：既不算在线，也不该凭空发「断了」信号（没连上 ≠ 刚断）
  CHECK_FALSE(session.linkAlive());
  CHECK(session.replyAgeMs() == -1);
  waitUntil([&] { return !flips.empty(); }, 200);
  CHECK(flips.empty());

  // buildFrame 的 body 是「cmd + 数据」，不是纯数据
  const std::vector<uint8_t> read_freq_body = {0x03};
  t->reply(radio::core::civ::buildFrame(
      0xE0, 0x00, std::span<const uint8_t>(read_freq_body)));
  CHECK(waitUntil([&] { return session.linkAlive(); }, 2000));
  CHECK(session.replyAgeMs() >= 0);
  REQUIRE(flips.size() == 1);
  CHECK(flips.at(0) == 1);

  // 之后装死：超过阈值必须报失联——这是清单 G-6 的代码侧依据
  CHECK(waitUntil([&] { return flips.size() == 2; }, 3000));
  REQUIRE(flips.size() == 2);
  CHECK(flips.at(1) == 0);
  CHECK_FALSE(session.linkAlive());
  CHECK(session.replyAgeMs() >= 500);
}

TEST_CASE("session: a NAK is surfaced to the UI instead of only hitting the log",
          "[session][nak]") {
  qtTestApp();
  auto t = std::make_shared<ScriptedTransport>();
  radio::app::RadioSession session(t);
  std::vector<int> naks;
  QObject::connect(&session, &radio::app::RadioSession::commandRejected,
                   [&naks](quint8 opcode) { naks.push_back(opcode); });
  session.start();

  const std::vector<uint8_t> nak_body = {radio::core::civ::kNak,
                                         0x03};  // 被拒的是设频命令
  t->reply(radio::core::civ::buildFrame(0xE0, 0x00,
                                       std::span<const uint8_t>(nak_body)));
  // emitFrame 是同步回调，不需要等事件循环
  REQUIRE(naks.size() == 1);
  CHECK(naks.at(0) == 0x03);
}
