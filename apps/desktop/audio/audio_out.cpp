// SPDX-License-Identifier: AGPL-3.0-or-later
#include "audio/audio_out.h"

#include <RtAudio.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace radio::audio {
namespace {
struct PlayCtx {
  const int16_t* data;
  size_t total;
  size_t pos;
  std::mutex mu;
};

int rtCallback(void* out, void* in, unsigned int nFrames, double,
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

AudioOut::AudioOut() : dac_(new RtAudio()) {}

AudioOut::~AudioOut() {
  stop();
  delete static_cast<RtAudio*>(dac_);
}

std::vector<std::string> AudioOut::deviceNames() const {
  std::vector<std::string> out;
  auto* rt = static_cast<RtAudio*>(dac_);
  for (uint32_t i = 0; i < rt->getDeviceCount(); ++i)
    out.push_back(rt->getDeviceInfo(i).name);
  return out;
}

bool AudioOut::play(const std::vector<int16_t>& pcm, uint32_t sample_rate,
                    uint32_t device_id, std::string* error) {
  auto* rt = static_cast<RtAudio*>(dac_);
  if (pcm.empty()) return true;
  // 新版 RtAudio：错误用返回码（RtAudioErrorType），不再抛异常
  if (rt->isStreamOpen()) rt->closeStream();
  RtAudio::StreamParameters p;
  p.deviceId = device_id;
  p.nChannels = 1;
  p.firstChannel = 0;
  RtAudio::StreamOptions opt;
  opt.flags = RTAUDIO_SCHEDULE_REALTIME;
  static PlayCtx ctx;
  ctx.data = pcm.data();
  ctx.total = pcm.size();
  ctx.pos = 0;
  unsigned int buffer_frames = 512;
  if (rt->openStream(&p, nullptr, RTAUDIO_SINT16, sample_rate, &buffer_frames,
                     &rtCallback, &ctx, &opt) != RTAUDIO_NO_ERROR) {
    if (error) *error = "openStream 失败（设备被占/无输出设备）";
    return false;
  }
  if (rt->startStream() != RTAUDIO_NO_ERROR) {
    rt->closeStream();
    if (error) *error = "startStream 失败";
    return false;
  }
  playing_ = true;
  // 阻塞到播完（SSTV 36s/72s/Martin M1 115s 均可接受；stop 可中断）
  while (playing_ && ctx.pos < ctx.total) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (rt->isStreamOpen() && rt->isStreamRunning()) rt->stopStream();
  rt->closeStream();
  playing_ = false;
  return true;
}

void AudioOut::stop() {
  if (!playing_) return;
  playing_ = false;  // 回调线程自行收尾
}

}  // namespace radio::audio
