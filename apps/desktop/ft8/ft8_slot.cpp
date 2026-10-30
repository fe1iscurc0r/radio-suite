// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ft8/ft8_slot.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace radio::ft8 {

SlotDecoder::SlotDecoder(uint32_t in_rate_hz)
    : ratio_(in_rate_hz / kSampleRateHz), in_rate_(in_rate_hz) {
  if (ratio_ == 0 || in_rate_hz % kSampleRateHz != 0)
    throw std::invalid_argument("FT8 槽解码器要求输入采样率是 12 kHz 的整数倍");
  current_.reserve(kSlotSamples12k);
  carry_.reserve(ratio_);
  worker_ = std::thread([this] { workerLoop(); });
}

SlotDecoder::~SlotDecoder() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    shutdown_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void SlotDecoder::setCallback(Callback cb) {
  std::lock_guard<std::mutex> lk(mu_);
  callback_ = std::move(cb);
}

int64_t SlotDecoder::start(int64_t utc_now_ms) {
  const int64_t skip_ms = (kSlotMs - utc_now_ms % kSlotMs) % kSlotMs;
  {
    std::lock_guard<std::mutex> lk(mu_);
    ++gen_;  // 在飞解码结果作废
    running_flag_ = true;
    current_.clear();
    carry_.clear();
    pending_ = Job{};
    skip_in_ = static_cast<size_t>(skip_ms * in_rate_ / 1000);
    slot_utc_ms_ = utc_now_ms + skip_ms;
  }
  fed_.store(0, std::memory_order_relaxed);
  running_.store(true, std::memory_order_relaxed);
  return skip_ms;
}

void SlotDecoder::stop() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    ++gen_;
    running_flag_ = false;
    current_.clear();
    carry_.clear();
    pending_ = Job{};
    skip_in_ = 0;
  }
  running_.store(false, std::memory_order_relaxed);
}

void SlotDecoder::push(const int16_t* samples, size_t count) {
  if (samples == nullptr || count == 0) return;
  if (!running_.load(std::memory_order_relaxed)) return;

  std::lock_guard<std::mutex> lk(mu_);
  if (!running_flag_) return;
  fed_.fetch_add(count, std::memory_order_relaxed);

  size_t pos = 0;
  if (skip_in_ > 0) {
    const size_t skip = std::min(skip_in_, count);
    skip_in_ -= skip;
    pos += skip;
  }
  if (pos == count) return;

  // 余数拼上本块后按 ratio_ 整组抽取（防块长不是 ratio_ 倍数时逐块漂移）
  carry_.insert(carry_.end(), samples + pos, samples + count);
  size_t i = 0;
  for (; i + ratio_ <= carry_.size(); i += ratio_) {
    int sum = 0;
    for (unsigned k = 0; k < ratio_; ++k) sum += carry_[i + k];
    current_.push_back(static_cast<float>(sum) /
                       (32768.0f * static_cast<float>(ratio_)));
    if (current_.size() >= kSlotSamples12k) submitSlotLocked();
  }
  carry_.erase(carry_.begin(), carry_.begin() + static_cast<ptrdiff_t>(i));
}

void SlotDecoder::submitSlotLocked() {
  if (!pending_.samples.empty()) {  // 上一槽还没解完：宁可丢一槽也不积压
    dropped_.fetch_add(1, std::memory_order_relaxed);
  } else {
    pending_.gen = gen_;
    pending_.utc_ms = slot_utc_ms_;
    pending_.samples = std::move(current_);
    cv_.notify_one();
  }
  current_.clear();
  current_.reserve(kSlotSamples12k);
  slot_utc_ms_ += kSlotMs;
}

void SlotDecoder::workerLoop() {
  std::unique_lock<std::mutex> lk(mu_);
  for (;;) {
    cv_.wait(lk, [this] { return shutdown_ || !pending_.samples.empty(); });
    if (shutdown_) return;

    Job job = std::move(pending_);
    pending_ = Job{};
    const uint64_t gen = gen_;
    lk.unlock();

    if (job.gen == gen) {
      decoding_.store(true, std::memory_order_relaxed);
      const auto t0 = std::chrono::steady_clock::now();
      SlotResult result;
      result.utc_ms = job.utc_ms;
      result.messages = decode(job.samples.data(), job.samples.size());
      result.decode_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - t0)
              .count();
      decoding_.store(false, std::memory_order_relaxed);
      slots_done_.fetch_add(1, std::memory_order_relaxed);

      Callback cb;
      {
        std::lock_guard<std::mutex> guard(mu_);
        if (gen == gen_) cb = callback_;
      }
      if (cb) cb(result);
    }
    lk.lock();
  }
}

double SlotDecoder::slotProgress() const {
  std::lock_guard<std::mutex> lk(mu_);
  return static_cast<double>(current_.size()) /
         static_cast<double>(kSlotSamples12k);
}

}  // namespace radio::ft8
