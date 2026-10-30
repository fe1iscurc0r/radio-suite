// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · audio/audio_out_android.cpp — Android 输出后端（NDK AAudio，API 26+）
// 桌面用 RtAudio（WASAPI/ALSA/CoreAudio），Android 没有对应后端，这里直接调 AAudio。
// 设备模型：AAudio 不做逐设备枚举（系统自动选最快路径），因此对外暴露单个「系统默认」。
#include "audio/audio_out.h"

#include <aaudio/AAudio.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace radio::audio {
namespace {

constexpr uint32_t kDefaultDeviceId = 0;
constexpr int32_t kFramesPerCallback = 480;  // 10ms @ 48k
constexpr int32_t kWriteChunk = 960;         // 20ms

std::string aaudioError(aaudio_result_t rc) {
  return std::string("AAudio: ") + AAudio_convertResultToText(rc);
}

// 打开输出流；cb 为空 = 阻塞写模式（AudioOut::play），非空 = 回调模式（AudioSink）。
AAudioStream* openOutput(uint32_t sample_rate,
                         aaudio_data_callback_result_t (*cb)(AAudioStream*,
                                                             void*, void*,
                                                             int32_t),
                         void* user, std::string* error) {
  AAudioStreamBuilder* builder = nullptr;
  aaudio_result_t rc = AAudio_createStreamBuilder(&builder);
  if (rc != AAUDIO_OK) {
    if (error) *error = aaudioError(rc);
    return nullptr;
  }
  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
  AAudioStreamBuilder_setChannelCount(builder, 1);
  AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(sample_rate));
  AAudioStreamBuilder_setPerformanceMode(builder,
                                         AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setDeviceId(builder, AAUDIO_UNSPECIFIED);
  if (cb != nullptr) {
    AAudioStreamBuilder_setDataCallback(builder, cb, user);
    AAudioStreamBuilder_setFramesPerDataCallback(builder, kFramesPerCallback);
  }
  AAudioStream* stream = nullptr;
  rc = AAudioStreamBuilder_openStream(builder, &stream);
  AAudioStreamBuilder_delete(builder);
  if (rc != AAUDIO_OK) {
    if (error) *error = aaudioError(rc);
    return nullptr;
  }
  return stream;
}

void closeStream(AAudioStream* stream) {
  if (stream == nullptr) return;
  AAudioStream_requestStop(stream);
  AAudioStream_close(stream);
}

}  // namespace

struct AudioOut::Impl {
  std::vector<int16_t> pcm;  // playAsync 持有音频副本
  std::thread worker;
  std::atomic<bool> playing{false};
  std::mutex write_mu;       // 保护当前写流（stop 可以从别的线程打断）
  AAudioStream* stream = nullptr;

  ~Impl() {
    if (worker.joinable()) worker.join();
    closeStream(stream);
    stream = nullptr;
  }
};

namespace {

// 阻塞写：AAudio 的 write 带超时，stop() 置 playing=false 后最迟 100ms 退出。
// 参数逐个传入，不引用 AudioOut::Impl（私有嵌套类型，匿名命名空间里不能命名）。
bool playBlocking(std::atomic<bool>& playing, std::mutex& write_mu,
                  AAudioStream*& slot, const std::vector<int16_t>& pcm,
                  uint32_t sample_rate, std::string* error) {
  if (pcm.empty()) return true;
  std::lock_guard<std::mutex> lock(write_mu);
  closeStream(slot);
  slot = openOutput(sample_rate, nullptr, nullptr, error);
  if (slot == nullptr) {
    playing = false;
    return false;
  }
  aaudio_result_t rc = AAudioStream_requestStart(slot);
  if (rc != AAUDIO_OK) {
    if (error) *error = aaudioError(rc);
    closeStream(slot);
    slot = nullptr;
    playing = false;
    return false;
  }
  playing = true;
  size_t pos = 0;
  while (pos < pcm.size() && playing.load()) {
    const int32_t frames = static_cast<int32_t>(
        std::min<size_t>(kWriteChunk, pcm.size() - pos));
    const aaudio_result_t n = AAudioStream_write(
        slot, pcm.data() + pos, frames, 100'000'000LL /* 100ms */);
    if (n < 0) {
      if (error) *error = aaudioError(n);
      break;
    }
    pos += static_cast<size_t>(n);
  }
  closeStream(slot);
  slot = nullptr;
  playing = false;
  return error == nullptr || error->empty();
}

}  // namespace

AudioOut::AudioOut() : impl_(std::make_unique<Impl>()) {}

AudioOut::~AudioOut() { wait(); }

int AudioOut::defaultOutputDevice() const { return static_cast<int>(kDefaultDeviceId); }

std::vector<std::string> AudioOut::deviceNames() const {
  return {"系统默认"};
}

bool AudioOut::play(const std::vector<int16_t>& pcm, uint32_t sample_rate,
                    uint32_t /*device_id*/, std::string* error) {
  if (impl_->worker.joinable()) {
    if (impl_->playing.load()) {
      if (error) *error = "后台播放进行中";
      return false;
    }
    impl_->worker.join();
  }
  return playBlocking(impl_->playing, impl_->write_mu, impl_->stream, pcm,
                      sample_rate, error);
}

bool AudioOut::playAsync(std::vector<int16_t> pcm, uint32_t sample_rate,
                         uint32_t /*device_id*/,
                         std::function<void(bool, std::string)> done) {
  if (impl_->worker.joinable()) {
    if (impl_->playing.load()) return false;
    impl_->worker.join();
  }
  if (pcm.empty()) {
    if (done) done(true, std::string());
    return true;
  }
  impl_->pcm = std::move(pcm);
  impl_->playing = true;  // 先占位再起线程（同桌面实现，避免 join 卡到播完）
  impl_->worker = std::thread([this, sample_rate, done = std::move(done)]() mutable {
    std::string err;
    const bool ok = playBlocking(impl_->playing, impl_->write_mu, impl_->stream,
                                 impl_->pcm, sample_rate, &err);
    if (done) done(ok, err);
  });
  return true;
}

void AudioOut::stop() { impl_->playing = false; }

void AudioOut::wait() {
  stop();
  if (impl_->worker.joinable()) impl_->worker.join();
  std::lock_guard<std::mutex> lock(impl_->write_mu);
  closeStream(impl_->stream);
  impl_->stream = nullptr;
}

bool AudioOut::playing() const { return impl_->playing.load(); }

// ---------------- AudioSink：50003 下行语音常开输出 ----------------

namespace {
struct SinkState {
  std::mutex mu;
  std::deque<int16_t> queue;
  size_t cap = 9600;  // 200ms @ 48k
  size_t dropped = 0;
  size_t underruns = 0;
  bool running = false;
};

aaudio_data_callback_result_t sinkCallback(AAudioStream*, void* user,
                                           void* audio_data, int32_t n_frames) {
  auto* st = static_cast<SinkState*>(user);
  auto* dst = static_cast<int16_t*>(audio_data);
  std::lock_guard<std::mutex> lock(st->mu);
  const size_t n = std::min<size_t>(static_cast<size_t>(n_frames), st->queue.size());
  for (size_t i = 0; i < n; ++i) {
    dst[i] = st->queue.front();
    st->queue.pop_front();
  }
  if (n < static_cast<size_t>(n_frames)) {
    std::memset(dst + n, 0, (static_cast<size_t>(n_frames) - n) * sizeof(int16_t));
    ++st->underruns;
  }
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
}  // namespace

struct AudioSink::Impl {
  SinkState state;
  AAudioStream* stream = nullptr;
  std::mutex stream_mu;
};

AudioSink::AudioSink() : impl_(std::make_unique<Impl>()) {}

AudioSink::~AudioSink() { stop(); }

int AudioSink::defaultOutputDevice() const { return static_cast<int>(kDefaultDeviceId); }

std::vector<uint32_t> AudioSink::outputDeviceIds() const { return {kDefaultDeviceId}; }

std::vector<std::string> AudioSink::outputDeviceNames() const { return {"系统默认"}; }

bool AudioSink::start(uint32_t /*device_id*/, uint32_t sample_rate,
                      std::string* error) {
  auto& st = impl_->state;
  if (running()) return true;
  std::lock_guard<std::mutex> lock(impl_->stream_mu);
  st.cap = static_cast<size_t>(sample_rate) / 5;  // 200ms
  AAudioStream* stream = openOutput(sample_rate, &sinkCallback, &st, error);
  if (stream == nullptr) return false;
  const aaudio_result_t rc = AAudioStream_requestStart(stream);
  if (rc != AAUDIO_OK) {
    if (error) *error = aaudioError(rc);
    closeStream(stream);
    return false;
  }
  {
    std::lock_guard<std::mutex> g(st.mu);
    st.queue.clear();
    st.dropped = 0;
    st.underruns = 0;
    st.running = true;
  }
  impl_->stream = stream;
  return true;
}

void AudioSink::stop() {
  auto& st = impl_->state;
  {
    std::lock_guard<std::mutex> lock(st.mu);
    if (!st.running) return;
    st.running = false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->stream_mu);
    closeStream(impl_->stream);
    impl_->stream = nullptr;
  }
  flush();
}

bool AudioSink::running() const {
  std::lock_guard<std::mutex> lock(impl_->state.mu);
  return impl_->state.running;
}

void AudioSink::push(std::span<const int16_t> pcm) {
  if (pcm.empty()) return;
  auto& st = impl_->state;
  std::lock_guard<std::mutex> lock(st.mu);
  if (!st.running) return;
  for (int16_t s : pcm) {
    if (st.queue.size() >= st.cap) {
      st.queue.pop_front();
      ++st.dropped;
    }
    st.queue.push_back(s);
  }
}

void AudioSink::flush() {
  std::lock_guard<std::mutex> lock(impl_->state.mu);
  impl_->state.queue.clear();
}

size_t AudioSink::queuedSamples() const {
  std::lock_guard<std::mutex> lock(impl_->state.mu);
  return impl_->state.queue.size();
}

size_t AudioSink::droppedSamples() const {
  std::lock_guard<std::mutex> lock(impl_->state.mu);
  return impl_->state.dropped;
}

size_t AudioSink::underruns() const {
  std::lock_guard<std::mutex> lock(impl_->state.mu);
  return impl_->state.underruns;
}

}  // namespace radio::audio
