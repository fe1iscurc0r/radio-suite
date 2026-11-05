// SPDX-License-Identifier: AGPL-3.0-or-later
// CI-V over 串口（A3：CivTcpGateway 缺的那一半）。
//
// 台架上没有 CAT 线，所以这套用例走注入的内存管道（fake_serial_pipe.h）：
// 会出错的地方——粘包、半包、脏前缀、超长无尾、开线/写失败——跟有没有硬件无关，
// 在 CI 上必须真跑，不能只靠代码审查。
#include <catch2/catch_test_macros.hpp>

#include <QByteArray>

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/civ.h"
#include "core/transport_mock.h"
#include "core/transport_serial.h"
#include "fake_serial_pipe.h"
#include "qt_test_app.h"

namespace {

using radio::core::SerialCivTransport;
using radio::test::FakeSerialPipe;

struct Probe {
  std::vector<std::vector<uint8_t>> frames;
};

// SerialCivTransport 有用户声明的析构（要收尾管道），所以没有隐式移动构造——
// 测试里统一用 unique_ptr 持有，别指望按值返回。
std::unique_ptr<SerialCivTransport> makeTransport(FakeSerialPipe*& pipe_out,
                                                  const std::string& port = "COM9") {
  auto pipe = std::make_unique<FakeSerialPipe>();
  pipe_out = pipe.get();
  SerialCivTransport::Config cfg;
  cfg.port_name = port;
  cfg.baud = 9600;
  return std::make_unique<SerialCivTransport>(std::move(cfg), std::move(pipe));
}

std::vector<uint8_t> vec(const std::initializer_list<int>& bytes) {
  std::vector<uint8_t> out;
  for (int b : bytes) out.push_back(static_cast<uint8_t>(b));
  return out;
}

}  // namespace

TEST_CASE("serial transport reports open failure without pretending", "[core][serial]") {
  qtTestApp();
  FakeSerialPipe* raw = nullptr;
  auto t = makeTransport(raw);
  raw->fail_open = true;
  std::string err;
  CHECK_FALSE(t->open(&err));
  CHECK_FALSE(err.empty());
  CHECK_FALSE(t->isOpen());
  // 开线失败之后 send 也必须拒绝，别把请求写进黑洞
  CHECK_FALSE(t->send(radio::core::civ::readFreqFrame()));
}

TEST_CASE("serial transport writes a request frame byte for byte", "[core][serial]") {
  qtTestApp();
  FakeSerialPipe* raw = nullptr;
  auto t = makeTransport(raw);
  std::string err;
  REQUIRE(t->open(&err));
  CHECK(t->isOpen());

  const auto req = radio::core::civ::readFreqFrame();
  REQUIRE(t->send(req));
  // 传输层不许重排、加前导或补尾：线上字节必须与 send 的入参逐字节相同
  CHECK(raw->written() == req);
  // 日志要能自证接的是哪条线、什么波特率
  CHECK(t->lastLog().find("opened COM9 @9600 8N1") != std::string::npos);
}

TEST_CASE("serial transport delivers a whole frame on poll", "[core][serial]") {
  qtTestApp();
  FakeSerialPipe* raw = nullptr;
  auto t = makeTransport(raw);
  REQUIRE(t->open(nullptr));
  Probe probe;
  t->setFrameCallback([&](std::span<const uint8_t> f) {
    probe.frames.emplace_back(f.begin(), f.end());
  });

  const auto reply = vec({0xFE, 0xFE, 0xE0, 0xA4, 0x03, 0xFD});
  raw->feed(reply);
  CHECK(t->poll() == 1);
  REQUIRE(probe.frames.size() == 1);
  CHECK(probe.frames[0] == reply);
  CHECK(t->framesDelivered() == 1);
  // 一次 poll 把缓冲读干净后，下一轮不该重复上抛同一条
  CHECK(t->poll() == 0);
  CHECK(probe.frames.size() == 1);
  // 更要紧的是「消费掉的字节必须从缓冲里去掉」：再来一条不同的帧，
  // 缓冲没.trim 的话旧帧会被再切一遍，流从此错位
  const auto second = vec({0xFE, 0xFE, 0xE0, 0xA4, 0xFB, 0x01, 0xFD});
  raw->feed(second);
  CHECK(t->poll() == 1);
  REQUIRE(probe.frames.size() == 2);
  CHECK(probe.frames[1] == second);
}

TEST_CASE("serial transport holds a half frame until the terminator arrives",
          "[core][serial]") {
  qtTestApp();
  FakeSerialPipe* raw = nullptr;
  auto t = makeTransport(raw);
  REQUIRE(t->open(nullptr));
  Probe probe;
  t->setFrameCallback([&](std::span<const uint8_t> f) {
    probe.frames.emplace_back(f.begin(), f.end());
  });

  const auto reply = vec({0xFE, 0xFE, 0xE0, 0xA4, 0x03, 0xFD});
  raw->feed(std::span<const uint8_t>(reply.data(), 4));  // 只到 0xE0，没收尾
  CHECK(t->poll() == 0);
  CHECK(probe.frames.empty());

  raw->feed(std::span<const uint8_t>(reply.data() + 4, reply.size() - 4));
  CHECK(t->poll() == 1);  // 补上尾才切得出这一帧
  REQUIRE(probe.frames.size() == 1);
  CHECK(probe.frames[0] == reply);
}

TEST_CASE("serial transport splits frames glued into one read", "[core][serial]") {
  qtTestApp();
  FakeSerialPipe* raw = nullptr;
  auto t = makeTransport(raw);
  REQUIRE(t->open(nullptr));
  Probe probe;
  t->setFrameCallback([&](std::span<const uint8_t> f) {
    probe.frames.emplace_back(f.begin(), f.end());
  });

  const auto a = vec({0xFE, 0xFE, 0xE0, 0xA4, 0x03, 0xFD});
  const auto b = vec({0xFE, 0xFE, 0xE0, 0xA4, 0xFB, 0x01, 0xFD});
  std::vector<uint8_t> glued;
  glued.insert(glued.end(), a.begin(), a.end());
  glued.insert(glued.end(), b.begin(), b.end());
  raw->feed(glued);

  CHECK(t->poll() == 2);
  REQUIRE(probe.frames.size() == 2);
  CHECK(probe.frames[0] == a);
  CHECK(probe.frames[1] == b);
}

TEST_CASE("serial transport drops junk before the preamble and counts it",
          "[core][serial]") {
  qtTestApp();
  FakeSerialPipe* raw = nullptr;
  auto t = makeTransport(raw);
  REQUIRE(t->open(nullptr));
  Probe probe;
  t->setFrameCallback([&](std::span<const uint8_t> f) {
    probe.frames.emplace_back(f.begin(), f.end());
  });

  const auto frame = vec({0xFE, 0xFE, 0xE0, 0xA4, 0x03, 0xFD});
  const auto junk = vec({0x00, 0x55, 0xAA});  // 上电毛刺/波特率错时的乱码
  std::vector<uint8_t> dirty;
  dirty.insert(dirty.end(), junk.begin(), junk.end());
  dirty.insert(dirty.end(), frame.begin(), frame.end());
  raw->feed(dirty);

  CHECK(t->poll() == 1);
  REQUIRE(probe.frames.size() == 1);
  CHECK(probe.frames[0] == frame);       // 垃圾不能污染上抛的帧
  CHECK(t->bytesDiscarded() == junk.size());
}

TEST_CASE("serial transport resyncs after a run with no terminator", "[core][serial]") {
  qtTestApp();
  FakeSerialPipe* raw = nullptr;
  auto t = makeTransport(raw);
  REQUIRE(t->open(nullptr));
  Probe probe;
  t->setFrameCallback([&](std::span<const uint8_t> f) {
    probe.frames.emplace_back(f.begin(), f.end());
  });

  // 攒了几千字节还见不到 FD = 流已经错位（对端不是 CI-V 或波特率不对）。
  // 继续攒着迟早吐出一堆假帧，丢掉重同步才诚实。
  std::vector<uint8_t> noise(5000, 0x41);
  raw->feed(noise);
  CHECK(t->poll() == 0);
  CHECK(t->bytesDiscarded() >= 4096);
  CHECK(t->lastLog().find("drop") != std::string::npos);

  // 丢掉之后链路要还能正常收帧，而不是从此哑掉
  const auto frame = vec({0xFE, 0xFE, 0xE0, 0xA4, 0x03, 0xFD});
  raw->feed(frame);
  CHECK(t->poll() == 1);
  REQUIRE(probe.frames.size() == 1);
  CHECK(probe.frames[0] == frame);
}

TEST_CASE("serial transport round trip answers a mock rig behind the fake wire",
          "[core][serial]") {
  qtTestApp();
  FakeSerialPipe* raw = nullptr;
  auto t = makeTransport(raw);

  // 把一台模拟电台接到这条虚拟线的另一头：写进来的请求帧交给 MockTransport，
  // 它产生的应答帧再喂回管道——等价于网关那侧真的接了根 CAT 线。
  auto rig = std::make_shared<radio::core::MockTransport>();
  REQUIRE(rig->open(nullptr));  // mock 电台没 open 之前 send() 一律拒绝，忘了这步就等于没接线
  rig->setFrameCallback([&](std::span<const uint8_t> f) {
    raw->feed(f);
  });
  raw->responder = [rig_ptr = rig.get()](std::span<const uint8_t> frame) {
    rig_ptr->send(frame);
    rig_ptr->poll();
  };

  REQUIRE(t->open(nullptr));
  std::vector<std::vector<uint8_t>> got;
  t->setFrameCallback([&](std::span<const uint8_t> f) {
    got.emplace_back(f.begin(), f.end());
  });

  REQUIRE(t->send(radio::core::civ::readFreqFrame()));
  CHECK(t->poll() == 1);  // 请求 → 电台 → 回帧，一次 poll 就该拿到
  REQUIRE(got.size() == 1);

  const auto parsed = radio::core::civ::parseFrame(got[0]);
  REQUIRE(parsed.has_value());
  CHECK(parsed->cmd == 0x03);  // 读频率的应答
  const auto hz = radio::core::civ::bcdToFreq(parsed->payload);
  REQUIRE(hz.has_value());
  CHECK(*hz == 14'270'000);  // mock 电台的出厂频率

  // 写失败要如实回报，别把「发出去了」当成假事实
  raw->fail_write = true;
  CHECK_FALSE(t->send(radio::core::civ::readFreqFrame()));
}
