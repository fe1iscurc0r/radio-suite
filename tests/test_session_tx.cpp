// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "app/session.h"
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
