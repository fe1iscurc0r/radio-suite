// SPDX-License-Identifier: AGPL-3.0-or-later
#include "aprs/afsk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace radio::aprs {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint8_t kFlagBits[8] = {0, 1, 1, 1, 1, 1, 1, 0};  // 0x7E LSB-first

uint16_t x25Crc(std::span<const uint8_t> data) {
  uint16_t crc = 0xFFFF;
  for (uint8_t b : data) {
    crc ^= b;
    for (int i = 0; i < 8; ++i)
      crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408)
                      : static_cast<uint16_t>(crc >> 1);
  }
  return static_cast<uint16_t>(~crc);
}

std::vector<uint8_t> withFcs(std::span<const uint8_t> frame) {
  std::vector<uint8_t> out(frame.begin(), frame.end());
  const uint16_t crc = x25Crc(frame);
  out.push_back(static_cast<uint8_t>(crc & 0xFF));
  out.push_back(static_cast<uint8_t>(crc >> 8));
  return out;
}

// 位流（LSB-first，含位填充），不含首尾标志
std::vector<uint8_t> stuffedBits(std::span<const uint8_t> data) {
  std::vector<uint8_t> bits;
  bits.reserve(data.size() * 9);
  int ones = 0;
  for (uint8_t b : data) {
    for (int i = 0; i < 8; ++i) {
      const uint8_t bit = static_cast<uint8_t>((b >> i) & 1);
      bits.push_back(bit);
      if (bit == 1) {
        ++ones;
        if (ones == 5) {
          bits.push_back(0);
          ones = 0;
        }
      } else {
        ones = 0;
      }
    }
  }
  return bits;
}

}  // namespace

std::vector<float> afskModulate(std::span<const uint8_t> frame,
                                const AfskConfig& cfg, int preamble_flags,
                                int tail_flags) {
  if (cfg.sample_rate == 0 || cfg.baud <= 0.0) return {};
  const auto data = withFcs(frame);
  const auto bits = stuffedBits(data);

  std::vector<uint8_t> stream;
  const int pre = std::max(1, preamble_flags);
  const int tail = std::max(1, tail_flags);
  stream.reserve(static_cast<size_t>(pre + tail) * 8 + bits.size() + 8);
  for (int i = 0; i < pre; ++i) stream.insert(stream.end(), kFlagBits, kFlagBits + 8);
  stream.insert(stream.end(), bits.begin(), bits.end());
  for (int i = 0; i < tail; ++i) stream.insert(stream.end(), kFlagBits, kFlagBits + 8);

  const double samples_per_bit = static_cast<double>(cfg.sample_rate) / cfg.baud;
  const double amp = 0.8;
  std::vector<float> out;
  out.reserve(static_cast<size_t>(stream.size() * samples_per_bit) + 2);
  double phase = 0.0;
  bool mark = true;
  double carry = 0.0;
  for (uint8_t bit : stream) {
    if (bit == 0) mark = !mark;  // NRZI：0 = 翻转
    const double freq = mark ? cfg.mark_hz : cfg.space_hz;
    const double inc = 2.0 * kPi * freq / static_cast<double>(cfg.sample_rate);
    carry += samples_per_bit;
    const int n = static_cast<int>(carry);
    carry -= n;
    for (int i = 0; i < n; ++i) {
      out.push_back(static_cast<float>(amp * std::sin(phase)));
      phase += inc;
      if (phase > 2.0 * kPi) phase -= 2.0 * kPi;
    }
  }
  return out;
}

AfskDemodulator::AfskDemodulator(AfskConfig cfg) : cfg_(cfg) {
  if (cfg_.sample_rate == 0) cfg_.sample_rate = 48000;
  if (cfg_.baud <= 0) cfg_.baud = 1200.0;
  bit_samples_ = static_cast<double>(cfg_.sample_rate) / cfg_.baud;
  const size_t n = static_cast<size_t>(std::max<long>(8, std::lround(bit_samples_ / 2.0)));
  window_.assign(n, 0.0f);
  cos_mark_.resize(n);
  sin_mark_.resize(n);
  cos_space_.resize(n);
  sin_space_.resize(n);
  for (size_t k = 0; k < n; ++k) {
    const double a = 2.0 * kPi * static_cast<double>(k) / cfg_.sample_rate;
    cos_mark_[k] = static_cast<float>(std::cos(a * cfg_.mark_hz));
    sin_mark_[k] = static_cast<float>(std::sin(a * cfg_.mark_hz));
    cos_space_[k] = static_cast<float>(std::cos(a * cfg_.space_hz));
    sin_space_[k] = static_cast<float>(std::sin(a * cfg_.space_hz));
  }
  sample_at_ = static_cast<double>(n) / 2.0;
}

void AfskDemodulator::feed(std::span<const int16_t> pcm) {
  const double delay = (static_cast<double>(window_.size()) - 1.0) / 2.0;
  for (int16_t raw : pcm) {
    window_[win_pos_] = static_cast<float>(raw) / 32768.0f;
    win_pos_ = (win_pos_ + 1) % window_.size();
    ++sample_index_;
    if (win_filled_ < window_.size()) {
      ++win_filled_;
      if (win_filled_ < window_.size()) continue;
    }

    double im = 0.0, qm = 0.0, is = 0.0, qs = 0.0;
    size_t idx = win_pos_;  // 最旧样本
    for (size_t k = 0; k < window_.size(); ++k) {
      const double x = window_[idx];
      im += x * cos_mark_[k];
      qm += x * sin_mark_[k];
      is += x * cos_space_[k];
      qs += x * sin_space_[k];
      idx = (idx + 1) % window_.size();
    }
    const double disc = (im * im + qm * qm) - (is * is + qs * qs);
    const bool crossing = have_disc_ && ((disc > 0.0) != (prev_disc_ > 0.0)) &&
                          std::fabs(disc) > 1e-9;
    prev_disc_ = disc;
    have_disc_ = true;

    if (crossing) {
      const int64_t mid = sample_index_ + static_cast<int64_t>(std::lround(delay + 1.0));
      if (!locked_) {
        locked_ = true;
        next_sample_index_ = mid;
      } else {
        // 只在半个比特内重同步，避免把时钟拽乱
        const int64_t err = mid - next_sample_index_;
        if (std::llabs(err) <= static_cast<int64_t>(std::lround(bit_samples_ / 2.0)))
          next_sample_index_ = mid;
      }
    }

    if (locked_ && sample_index_ >= next_sample_index_ &&
        sample_index_ - last_tone_sample_ >= static_cast<int64_t>(bit_samples_ * 0.75)) {
      onTone(disc > 0.0, sample_index_);
      last_tone_sample_ = sample_index_;
      next_sample_index_ += static_cast<int64_t>(std::lround(bit_samples_));
      if (next_sample_index_ <= sample_index_)
        next_sample_index_ = sample_index_ + static_cast<int64_t>(std::lround(bit_samples_));
    }
  }
}

void AfskDemodulator::onTone(bool mark, int64_t /*sample_index*/) {
  ++tones_;
  if (!have_tone_) {
    have_tone_ = true;
    last_tone_ = mark;
    return;
  }
  const int bit = mark == last_tone_ ? 1 : 0;
  last_tone_ = mark;
  onBit(bit);
}

void AfskDemodulator::onBit(int bit) {
  ++bits_in_;
  bits_.push_back(static_cast<uint8_t>(bit));
  if (bits_.size() > 8000) {
    ++overflows_;
    bits_.clear();
  }

  flag_shift_ = static_cast<uint8_t>((flag_shift_ >> 1) | (bit << 7));
  if (flag_shift_count_ < 8) ++flag_shift_count_;
  if (flag_shift_count_ >= 8 && flag_shift_ == 0x7E) {
    // 当前 8 位是标志，先从帧位流里剔除
    bits_.resize(bits_.size() >= 8 ? bits_.size() - 8 : 0);
    onFlag();
  }
}

void AfskDemodulator::onFlag() {
  ++flags_;
  if (bits_.size() >= 17 * 8) emitFrame();
  bits_.clear();
  flag_shift_ = 0;
  flag_shift_count_ = 0;
}

void AfskDemodulator::emitFrame() {
  // 去位填充：五个 1 后的 0 是填充位；六个 1 说明帧已损坏
  std::vector<uint8_t> raw;
  raw.reserve(bits_.size());
  int ones = 0;
  for (uint8_t b : bits_) {
    if (ones == 5) {
      if (b == 0) {
        ones = 0;
        continue;
      }
      ++crc_errors_;
      return;
    }
    raw.push_back(b);
    ones = b ? ones + 1 : 0;
  }
  if (raw.size() < 17 * 8) return;

  std::vector<uint8_t> bytes;
  bytes.reserve(raw.size() / 8);
  for (size_t i = 0; i + 8 <= raw.size(); i += 8) {
    uint8_t b = 0;
    for (size_t k = 0; k < 8; ++k) b = static_cast<uint8_t>(b | (raw[i + k] << k));
    bytes.push_back(b);
  }
  if (bytes.size() < 19) return;
  const uint16_t fcs = static_cast<uint16_t>(
      bytes[bytes.size() - 2] | (bytes[bytes.size() - 1] << 8));
  bytes.resize(bytes.size() - 2);  // 去 FCS
  if (x25Crc(bytes) != fcs) {
    ++crc_errors_;
    return;
  }
  ready_.push_back(std::move(bytes));
  ++frames_;
}

bool AfskDemodulator::pop(std::vector<uint8_t>* frame) {
  if (ready_.empty()) return false;
  if (frame) *frame = std::move(ready_.front());
  ready_.erase(ready_.begin());
  return true;
}

void AfskDemodulator::reset() {
  std::fill(window_.begin(), window_.end(), 0.0f);
  win_pos_ = 0;
  win_filled_ = 0;
  prev_disc_ = 0.0;
  have_disc_ = false;
  sample_index_ = 0;
  locked_ = false;
  next_sample_index_ = -1;
  last_tone_sample_ = -1000000;
  have_tone_ = false;
  last_tone_ = false;
  bits_.clear();
  flag_shift_ = 0;
  flag_shift_count_ = 0;
  ready_.clear();
}

}  // namespace radio::aprs
