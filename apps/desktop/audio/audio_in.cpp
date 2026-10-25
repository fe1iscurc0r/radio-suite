// SPDX-License-Identifier: AGPL-3.0-or-later
#include "audio/audio_in.h"

#include <RtAudio.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace radio::audio {
namespace {

using FrameCallback = AudioIn::FrameCallback;  // std::function（头文件 span 版）

struct CaptureCtx {
  FrameCallback cb;
  // 跨回调累积：RtAudio 一帧缓冲未必恰好 960 样本
  std::vector<int16_t> residue;
  std::mutex mu;
};

int captureCallback(void* out, void* in, unsigned int nFrames, double,
                    RtAudioStreamStatus status, void* user) {
  auto* ctx = static_cast<CaptureCtx*>(user);
  if (status != 0 || in == nullptr) return 0;
  const auto* src = static_cast<const int16_t*>(in);
  std::lock_guard<std::mutex> lock(ctx->mu);
  ctx->residue.insert(ctx->residue.end(), src, src + nFrames);
  // 按整 20ms 帧切分上抛（余料留下一轮回填——上游 kappanhang rec 环同语义）
  while (ctx->residue.size() >= kTxFrameSamples) {
    ctx->cb(std::span<const int16_t>(ctx->residue.data(), kTxFrameSamples));
    ctx->residue.erase(ctx->residue.begin(),
                       ctx->residue.begin() + kTxFrameSamples);
  }
  return 0;
}

}  // namespace

AudioIn::AudioIn() : dac_(new RtAudio()) {}

AudioIn::~AudioIn() {
  stop();
  delete static_cast<RtAudio*>(dac_);
}

std::vector<std::string> AudioIn::deviceNames() const {
  std::vector<std::string> out;
  auto* rt = static_cast<RtAudio*>(dac_);
  for (auto id : rt->getDeviceIds()) {
    // 只列有输入通道的设备（麦克风/线路入）
    if (rt->getDeviceInfo(id).inputChannels > 0)
      out.push_back(rt->getDeviceInfo(id).name);
  }
  return out;
}

std::vector<uint32_t> AudioIn::inputDeviceIds() const {
  std::vector<uint32_t> out;
  auto* rt = static_cast<RtAudio*>(dac_);
  for (auto id : rt->getDeviceIds())
    if (rt->getDeviceInfo(id).inputChannels > 0) out.push_back(id);
  return out;
}

bool AudioIn::start(uint32_t device_id, FrameCallback cb, std::string* error) {
  if (capturing_) return true;
  auto* rt = static_cast<RtAudio*>(dac_);
  auto* ctx = new CaptureCtx();
  ctx->cb = std::move(cb);
  RtAudio::StreamParameters p;
  p.deviceId = device_id;
  p.nChannels = 1;
  p.firstChannel = 0;
  unsigned int buffer_frames = 480;  // 10ms 粒度，余料累积到 20ms 帧
  try {
    rt->openStream(nullptr, &p, RTAUDIO_SINT16, kTxCaptureRate, &buffer_frames,
                   &captureCallback, ctx);
    rt->startStream();
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    delete ctx;
    return false;
  }
  ctx_ = ctx;
  capturing_ = true;
  return true;
}

void AudioIn::stop() {
  if (!capturing_) return;
  capturing_ = false;
  auto* rt = static_cast<RtAudio*>(dac_);
  auto* ctx = static_cast<CaptureCtx*>(ctx_);
  if (rt->isStreamOpen()) {
    if (rt->isStreamRunning()) rt->abortStream();
    rt->closeStream();
  }
  delete ctx;
  ctx_ = nullptr;
}

}  // namespace radio::audio
