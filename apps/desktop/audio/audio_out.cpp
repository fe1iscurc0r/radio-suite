// SPDX-License-Identifier: AGPL-3.0-or-later
#include "audio/audio_out.h"

#include <RtAudio.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace radio::audio {
namespace {
struct PlayCtx {
  const int16_t* data = nullptr;
  size_t total = 0;
  size_t pos = 0;
  std::mutex mu;
};

int rtCallback(void* out, void* /*in*/, unsigned int nFrames, double,
               RtAudioStreamStatus, void* user) {
  auto* ctx = static_cast<PlayCtx*>(user);
  auto* dst = static_cast<int16_t*>(out);
  std::lock_guard<std::mutex> lock(ctx->mu);
  const size_t n = std::min<size_t>(nFrames, ctx->total - ctx->pos);
  std::memcpy(dst, ctx->data + ctx->pos, n * sizeof(int16_t));
  ctx->pos += n;
  if (n < nFrames) std::memset(dst + n, 0, (nFrames - n) * sizeof(int16_t));
  return (ctx->pos >= ctx->total) ? 1 : 0;  // 播完停
}
}  // namespace

struct AudioOut::Impl {
  RtAudio rt;
  PlayCtx ctx;
  std::vector<int16_t> pcm;  // playAsync 持有音频副本
  std::thread worker;
  std::atomic<bool> playing{false};
};

namespace {

// 阻塞播放的公共实现；调用者保证不与另一个播放重叠。
bool playBlocking(RtAudio& rt, PlayCtx& ctx, const std::vector<int16_t>& pcm,
                  uint32_t sample_rate, uint32_t device_id,
                  std::atomic<bool>& playing, std::string* error) {
  if (pcm.empty()) return true;
  // 新版 RtAudio：错误用返回码（RtAudioErrorType），不再抛异常
  if (rt.isStreamOpen()) rt.closeStream();
  RtAudio::StreamParameters p;
  p.deviceId = device_id;
  p.nChannels = 1;
  p.firstChannel = 0;
  RtAudio::StreamOptions opt;
  opt.flags = RTAUDIO_SCHEDULE_REALTIME;
  {
    std::lock_guard<std::mutex> lock(ctx.mu);
    ctx.data = pcm.data();
    ctx.total = pcm.size();
    ctx.pos = 0;
  }
  unsigned int buffer_frames = 512;
  if (rt.openStream(&p, nullptr, RTAUDIO_SINT16, sample_rate, &buffer_frames,
                    &rtCallback, &ctx, &opt) != RTAUDIO_NO_ERROR) {
    if (error) *error = "openStream 失败（设备被占/无输出设备）";
    playing = false;
    return false;
  }
  if (rt.startStream() != RTAUDIO_NO_ERROR) {
    rt.closeStream();
    if (error) *error = "startStream 失败";
    playing = false;
    return false;
  }
  playing = true;
  // 阻塞到播完（SSTV 36s/72s/Martin M1 115s 均可接受；stop 可中断）
  while (playing.load() && ctx.pos < ctx.total) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (rt.isStreamOpen() && rt.isStreamRunning()) rt.stopStream();
  rt.closeStream();
  playing = false;
  return true;
}

}  // namespace

AudioOut::AudioOut() : impl_(std::make_unique<Impl>()) {}

AudioOut::~AudioOut() { wait(); }

int AudioOut::defaultOutputDevice() const {
  try {
    const uint32_t id = impl_->rt.getDefaultOutputDevice();
    if (impl_->rt.getDeviceInfo(id).outputChannels > 0)
      return static_cast<int>(id);
  } catch (const std::exception&) {
  }
  return -1;
}

std::vector<std::string> AudioOut::deviceNames() const {
  std::vector<std::string> out;
  for (uint32_t i = 0; i < impl_->rt.getDeviceCount(); ++i)
    out.push_back(impl_->rt.getDeviceInfo(i).name);
  return out;
}

bool AudioOut::play(const std::vector<int16_t>& pcm, uint32_t sample_rate,
                    uint32_t device_id, std::string* error) {
  if (impl_->worker.joinable()) {
    if (impl_->playing.load()) {
      if (error) *error = "后台播放进行中";
      return false;
    }
    impl_->worker.join();  // 上一段已播完，回收线程
  }
  return playBlocking(impl_->rt, impl_->ctx, pcm, sample_rate, device_id,
                      impl_->playing, error);
}

bool AudioOut::playAsync(std::vector<int16_t> pcm, uint32_t sample_rate,
                         uint32_t device_id,
                         std::function<void(bool, std::string)> done) {
  if (impl_->worker.joinable()) {
    if (impl_->playing.load()) return false;
    impl_->worker.join();  // 上一段已播完，回收线程
  }
  if (pcm.empty()) {
    if (done) done(true, std::string());
    return true;
  }
  impl_->pcm = std::move(pcm);
  // 先占位再起线程：否则下一段调用可能落在"已起线程/尚未开流"的窗口里，
  // join 会把 GUI 线程卡到整段播完。
  impl_->playing = true;
  impl_->worker = std::thread([this, sample_rate, device_id,
                               done = std::move(done)]() mutable {
    std::string err;
    const bool ok = playBlocking(impl_->rt, impl_->ctx, impl_->pcm,
                                 sample_rate, device_id, impl_->playing, &err);
    if (done) done(ok, err);
  });
  return true;
}

void AudioOut::stop() {
  // 回调线程/播放线程自行收尾（最多 50ms 轮询间隔）
  impl_->playing = false;
}

void AudioOut::wait() {
  stop();
  if (impl_->worker.joinable()) impl_->worker.join();
  if (impl_->rt.isStreamOpen()) {
    if (impl_->rt.isStreamRunning()) impl_->rt.stopStream();
    impl_->rt.closeStream();
  }
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

int sinkCallback(void* out, void*, unsigned int nFrames, double,
                 RtAudioStreamStatus, void* user) {
  auto* st = static_cast<SinkState*>(user);
  auto* dst = static_cast<int16_t*>(out);
  std::lock_guard<std::mutex> lock(st->mu);
  const size_t n = std::min<size_t>(nFrames, st->queue.size());
  for (size_t i = 0; i < n; ++i) {
    dst[i] = st->queue.front();
    st->queue.pop_front();
  }
  if (n < nFrames) {
    std::memset(dst + n, 0, (nFrames - n) * sizeof(int16_t));
    ++st->underruns;
  }
  return 0;
}
}  // namespace

struct AudioSink::Impl {
  RtAudio rt;
  SinkState state;
};

AudioSink::AudioSink() : impl_(std::make_unique<Impl>()) {}

AudioSink::~AudioSink() { stop(); }

int AudioSink::defaultOutputDevice() const {
  try {
    const uint32_t id = impl_->rt.getDefaultOutputDevice();
    if (impl_->rt.getDeviceInfo(id).outputChannels > 0)
      return static_cast<int>(id);
  } catch (const std::exception&) {
  }
  return -1;
}

bool AudioSink::start(uint32_t device_id, uint32_t sample_rate,
                      std::string* error) {
  auto& st = impl_->state;
  if (st.running) return true;
  RtAudio::StreamParameters p;
  p.deviceId = device_id;
  p.nChannels = 1;
  p.firstChannel = 0;
  RtAudio::StreamOptions opt;
  opt.flags = RTAUDIO_SCHEDULE_REALTIME;
  unsigned int buffer_frames = 480;  // 10ms
  st.cap = sample_rate / 5;          // 200ms
  try {
    if (impl_->rt.openStream(&p, nullptr, RTAUDIO_SINT16, sample_rate,
                             &buffer_frames, &sinkCallback, &st,
                             &opt) != RTAUDIO_NO_ERROR) {
      if (error) *error = "输出设备打开失败（被占用/无设备）";
      return false;
    }
    if (impl_->rt.startStream() != RTAUDIO_NO_ERROR) {
      impl_->rt.closeStream();
      if (error) *error = "输出流启动失败";
      return false;
    }
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(st.mu);
    st.queue.clear();
    st.dropped = 0;
    st.underruns = 0;
    st.running = true;
  }
  return true;
}

void AudioSink::stop() {
  auto& st = impl_->state;
  {
    std::lock_guard<std::mutex> lock(st.mu);
    if (!st.running) return;
    st.running = false;
  }
  if (impl_->rt.isStreamOpen()) {
    if (impl_->rt.isStreamRunning()) impl_->rt.stopStream();
    impl_->rt.closeStream();
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
