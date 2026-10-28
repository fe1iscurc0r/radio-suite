// SPDX-License-Identifier: AGPL-3.0-or-later
// 50003 上行门禁：TX 才发、离开 TX 清队列、有界丢最旧、链路未就绪不积压
#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

#include "app/audio_bridge.h"

namespace {

class FakeAudioLink : public radio::core::IAudioLink {
 public:
  bool ready = true;
  bool fail_send = false;
  int sent = 0;
  std::vector<std::vector<int16_t>> frames;

  bool audioReady() const override { return ready; }
  void setRxAudioCallback(RxCallback) override {}
  bool sendTxAudio(std::span<const int16_t> pcm) override {
    if (!ready || fail_send) return false;
    frames.emplace_back(pcm.begin(), pcm.end());
    ++sent;
    return true;
  }
};

std::vector<int16_t> pcmFrame(int16_t fill) {
  return std::vector<int16_t>(radio::core::kNetAudioSamplesPerFrame, fill);
}

}  // namespace

TEST_CASE("audio bridge: mic frames only queued in TX phase", "[audio]") {
  FakeAudioLink link;
  radio::app::AudioBridge bridge(&link, 5);
  const auto frame = pcmFrame(7);

  bridge.pushMic(frame);
  CHECK(bridge.queued() == 0);
  CHECK(bridge.drain() == 0);
  CHECK(link.sent == 0);

  bridge.setTxActive(true);
  REQUIRE(bridge.txActive());
  CHECK_FALSE(bridge.shouldPlayRx());
  bridge.pushMic(frame);
  bridge.pushMic(frame);
  CHECK(bridge.queued() == 2);
  CHECK(bridge.drain() == 2);
  CHECK(link.sent == 2);
  REQUIRE(link.frames.size() == 2);
  CHECK(link.frames[0] == frame);
}

TEST_CASE("audio bridge: bounded queue drops oldest", "[audio]") {
  FakeAudioLink link;
  radio::app::AudioBridge bridge(&link, 3);
  bridge.setTxActive(true);
  for (int16_t i = 1; i <= 4; ++i) bridge.pushMic(pcmFrame(i));

  CHECK(bridge.queued() == 3);
  CHECK(bridge.dropped() == 1);
  CHECK(bridge.drain() == 3);
  REQUIRE(link.frames.size() == 3);
  // 最旧的第 1 帧被丢掉，发送顺序从 2 开始
  CHECK(link.frames[0][0] == 2);
  CHECK(link.frames[2][0] == 4);
}

TEST_CASE("audio bridge: leaving TX clears queued mic audio", "[audio]") {
  FakeAudioLink link;
  radio::app::AudioBridge bridge(&link, 5);
  bridge.setTxActive(true);
  bridge.pushMic(pcmFrame(9));
  bridge.setTxActive(false);

  CHECK(bridge.queued() == 0);
  CHECK(bridge.txActive() == false);
  CHECK(bridge.shouldPlayRx());
  CHECK(bridge.drain() == 0);
  CHECK(link.sent == 0);
}

TEST_CASE("audio bridge: not-ready link leaves no backlog", "[audio]") {
  FakeAudioLink link;
  link.ready = false;
  radio::app::AudioBridge bridge(&link, 5);
  bridge.setTxActive(true);
  bridge.pushMic(pcmFrame(3));
  bridge.pushMic(pcmFrame(3));
  CHECK_FALSE(bridge.linkReady());
  CHECK(bridge.drain() == 0);
  CHECK(bridge.queued() == 0);  // 就绪前不积压，避免恢复后补发旧语音
  CHECK(link.sent == 0);
}

TEST_CASE("audio bridge: send failure keeps frames for retry", "[audio]") {
  FakeAudioLink link;
  radio::app::AudioBridge bridge(&link, 5);
  bridge.setTxActive(true);
  bridge.pushMic(pcmFrame(4));
  bridge.pushMic(pcmFrame(5));

  link.fail_send = true;
  CHECK(bridge.drain() == 0);
  CHECK(bridge.queued() == 2);

  link.fail_send = false;
  CHECK(bridge.drain() == 2);
  CHECK(link.sent == 2);
}

TEST_CASE("audio bridge: empty frame ignored", "[audio]") {
  FakeAudioLink link;
  radio::app::AudioBridge bridge(&link, 2);
  bridge.setTxActive(true);
  bridge.pushMic({});
  CHECK(bridge.queued() == 0);
  CHECK(bridge.drain() == 0);
}
