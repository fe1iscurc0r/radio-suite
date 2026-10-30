// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · audio/audio_in_android.cpp — Android 麦克风采集（NDK AAudio，API 26+）
// 与桌面版同一语义：48kHz s16le 单声道，攒满 20ms（960 样本）回调一次。
// 注意：Android 需要 RECORD_AUDIO 运行时权限，由 app 层在 start() 之前申请
//       （Qt 6.7：QMicrophonePermission），没权限时 AAudio 开流会返回错误码。
#include "audio/audio_in.h"

#include <aaudio/AAudio.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace radio::audio {
namespace {

constexpr uint32_t kDefaultDeviceId = 0;
constexpr int32_t kFramesPerCallback = 480;  // 10ms，余料攒到 20ms

std::string aaudioError(aaudio_result_t rc) {
  return std::string("AAudio: ") + AAudio_convertResultToText(rc);
}

struct CaptureCtx {
  AudioIn::FrameCallback cb;
  std::vector<int16_t> residue;  // 跨回调累积到整 20ms 帧
  std::mutex mu;
  std::atomic<size_t>* overflows = nullptr;
};

aaudio_data_callback_result_t captureCallback(AAudioStream*, void* user,
                                              void* audio_data, int32_t n_frames) {
  auto* ctx = static_cast<CaptureCtx*>(user);
  if (audio_data == nullptr || n_frames <= 0) {
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }
  const auto* src = static_cast<const int16_t*>(audio_data);
  std::lock_guard<std::mutex> lock(ctx->mu);
  ctx->residue.insert(ctx->residue.end(), src, src + n_frames);
  while (ctx->residue.size() >= kTxFrameSamples) {
    ctx->cb(std::span<const int16_t>(ctx->residue.data(), kTxFrameSamples));
    ctx->residue.erase(ctx->residue.begin(),
                       ctx->residue.begin() + kTxFrameSamples);
  }
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void errorCallback(AAudioStream*, void* user, aaudio_result_t error) {
  auto* ctx = static_cast<CaptureCtx*>(user);
  if (ctx != nullptr && ctx->overflows != nullptr && error != AAUDIO_OK) {
    ctx->overflows->fetch_add(1, std::memory_order_relaxed);
  }
}

}  // namespace

AudioIn::AudioIn() = default;

AudioIn::~AudioIn() { stop(); }

std::vector<std::string> AudioIn::deviceNames() const {
  return {"麦克风（默认）"};
}

std::vector<uint32_t> AudioIn::inputDeviceIds() const { return {kDefaultDeviceId}; }

int AudioIn::defaultInputDevice() const { return static_cast<int>(kDefaultDeviceId); }

bool AudioIn::start(uint32_t /*device_id*/, FrameCallback cb, std::string* error) {
  if (capturing_) return true;
  auto* ctx = new CaptureCtx();
  ctx->cb = std::move(cb);
  ctx->overflows = &overflows_;
  overflows_.store(0, std::memory_order_relaxed);

  AAudioStreamBuilder* builder = nullptr;
  aaudio_result_t rc = AAudio_createStreamBuilder(&builder);
  if (rc != AAUDIO_OK) {
    if (error) *error = aaudioError(rc);
    delete ctx;
    return false;
  }
  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
  AAudioStreamBuilder_setChannelCount(builder, 1);
  AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(kTxCaptureRate));
  AAudioStreamBuilder_setPerformanceMode(builder,
                                         AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setDeviceId(builder, AAUDIO_UNSPECIFIED);
  AAudioStreamBuilder_setFramesPerDataCallback(builder, kFramesPerCallback);
  AAudioStreamBuilder_setDataCallback(builder, &captureCallback, ctx);
  AAudioStreamBuilder_setErrorCallback(builder, &errorCallback, ctx);
  AAudioStream* stream = nullptr;
  rc = AAudioStreamBuilder_openStream(builder, &stream);
  AAudioStreamBuilder_delete(builder);
  if (rc != AAUDIO_OK) {
    // 最常见原因：没申请 RECORD_AUDIO 权限
    if (error) *error = "麦克风打开失败：" + aaudioError(rc);
    delete ctx;
    return false;
  }
  rc = AAudioStream_requestStart(stream);
  if (rc != AAUDIO_OK) {
    if (error) *error = "麦克风启动失败：" + aaudioError(rc);
    AAudioStream_close(stream);
    delete ctx;
    return false;
  }
  dac_ = stream;
  ctx_ = ctx;
  capturing_ = true;
  return true;
}

void AudioIn::stop() {
  if (!capturing_) return;
  capturing_ = false;
  auto* stream = static_cast<AAudioStream*>(dac_);
  if (stream != nullptr) {
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
  }
  dac_ = nullptr;
  delete static_cast<CaptureCtx*>(ctx_);
  ctx_ = nullptr;
}

}  // namespace radio::audio
