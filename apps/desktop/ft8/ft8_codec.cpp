// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ft8/ft8_codec.h"

#include <common/monitor.h>
#include <ft8/constants.h>
#include <ft8/decode.h>
#include <ft8/encode.h>
#include <ft8/message.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace radio::ft8 {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSymbolBt = 2.0f;  // FT8 GFSK 平滑带宽因子（上游 gen_ft8）
constexpr float kGfskConstK = 5.336446f;  // pi * sqrt(2 / ln 2)

struct Monitor {
  monitor_t m{};
  bool ready = false;
  ~Monitor() {
    if (ready) monitor_free(&m);
  }
};

void gfskPulse(int n_spsym, float* pulse) {
  for (int i = 0; i < 3 * n_spsym; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(n_spsym) - 1.5f;
    const float arg1 = kGfskConstK * kSymbolBt * (t + 0.5f);
    const float arg2 = kGfskConstK * kSymbolBt * (t - 0.5f);
    pulse[i] = (std::erff(arg1) - std::erff(arg2)) / 2.0f;
  }
}

}  // namespace

std::vector<DecodedMessage> decode(const float* samples, size_t count,
                                   const DecodeOptions& opts) {
  std::vector<DecodedMessage> out;
  if (samples == nullptr || count == 0) return out;

  Monitor mon;
  monitor_config_t cfg{};
  cfg.f_min = opts.f_min;
  cfg.f_max = opts.f_max;
  cfg.sample_rate = kSampleRateHz;
  cfg.time_osr = opts.time_osr;
  cfg.freq_osr = opts.freq_osr;
  cfg.protocol = FTX_PROTOCOL_FT8;
  monitor_init(&mon.m, &cfg);
  mon.ready = true;

  for (size_t pos = 0; pos + static_cast<size_t>(mon.m.block_size) <= count;
       pos += static_cast<size_t>(mon.m.block_size))
    monitor_process(&mon.m, samples + pos);
  if (mon.m.wf.num_blocks == 0) return out;

  std::vector<ftx_candidate_t> heap(static_cast<size_t>(opts.max_candidates));
  const int found = ftx_find_candidates(&mon.m.wf, opts.max_candidates,
                                        heap.data(), opts.min_score);

  std::vector<std::array<uint8_t, FTX_PAYLOAD_LENGTH_BYTES>> seen;
  for (int i = 0; i < found; ++i) {
    const ftx_candidate_t& cand = heap[static_cast<size_t>(i)];
    ftx_message_t msg;
    ftx_decode_status_t status{};
    if (!ftx_decode_candidate(&mon.m.wf, &cand, opts.ldpc_iterations, &msg,
                              &status))
      continue;

    std::array<uint8_t, FTX_PAYLOAD_LENGTH_BYTES> payload{};
    std::memcpy(payload.data(), msg.payload, payload.size());
    if (std::find(seen.begin(), seen.end(), payload) != seen.end()) continue;

    ftx_message_offsets_t offsets{};
    char text[FTX_MAX_MESSAGE_LENGTH] = {0};
    if (ftx_message_decode(&msg, nullptr, text, &offsets) != FTX_MESSAGE_RC_OK)
      continue;  // 打包出未知类型：宁缺毋滥
    seen.push_back(payload);

    DecodedMessage dm;
    dm.text = text;
    dm.score = cand.score;
    dm.snr_db = static_cast<float>(cand.score) * 0.5f;
    dm.dt_s = (cand.time_offset + static_cast<float>(cand.time_sub) / mon.m.wf.time_osr) *
              mon.m.symbol_period;
    dm.df_hz = (mon.m.min_bin + cand.freq_offset +
                static_cast<float>(cand.freq_sub) / mon.m.wf.freq_osr) /
               mon.m.symbol_period;
    out.push_back(std::move(dm));
  }

  std::sort(out.begin(), out.end(),
            [](const DecodedMessage& a, const DecodedMessage& b) {
              return a.score > b.score;
            });
  return out;
}

std::vector<uint8_t> encodeTones(const std::string& text) {
  ftx_message_t msg;
  const ftx_message_rc_t rc = ftx_message_encode(&msg, nullptr, text.c_str());
  if (rc != FTX_MESSAGE_RC_OK)
    throw std::runtime_error("FT8 消息无法打包: " + text +
                             " (rc=" + std::to_string(static_cast<int>(rc)) + ")");
  std::vector<uint8_t> tones(FT8_NN);
  ft8_encode(msg.payload, tones.data());
  return tones;
}

std::vector<float> synthesize(const std::vector<uint8_t>& tones, float f0_hz) {
  if (tones.size() != FT8_NN) throw std::invalid_argument("FT8 音调数必须是 79");
  const int n_spsym = static_cast<int>(0.5f + kSampleRateHz * FT8_SYMBOL_PERIOD);
  const int n_sym = static_cast<int>(tones.size());
  const int n_wave = n_sym * n_spsym;
  const int n_slot = static_cast<int>(kSampleRateHz * FT8_SLOT_TIME);
  const int n_lead = (n_slot - n_wave) / 2;

  // 平滑频率波形：前后各延一个符号（上游 gen_ft8 的 dummy symbols）
  std::vector<float> dphi(static_cast<size_t>(n_wave) + 2 * n_spsym,
                          2.0f * kPi * f0_hz / kSampleRateHz);
  const float dphi_peak = 2.0f * kPi / static_cast<float>(n_spsym);
  std::vector<float> pulse(static_cast<size_t>(3 * n_spsym));
  gfskPulse(n_spsym, pulse.data());
  for (int i = 0; i < n_sym; ++i) {
    const int base = i * n_spsym;
    for (int j = 0; j < 3 * n_spsym; ++j)
      dphi[static_cast<size_t>(j + base)] +=
          dphi_peak * tones[static_cast<size_t>(i)] * pulse[static_cast<size_t>(j)];
  }
  for (int j = 0; j < 2 * n_spsym; ++j) {
    dphi[static_cast<size_t>(j)] +=
        dphi_peak * pulse[static_cast<size_t>(j + n_spsym)] * tones[0];
    dphi[static_cast<size_t>(j + n_sym * n_spsym)] +=
        dphi_peak * pulse[static_cast<size_t>(j)] * tones[static_cast<size_t>(n_sym - 1)];
  }

  std::vector<float> audio(static_cast<size_t>(n_slot), 0.0f);
  float phi = 0.0f;
  for (int k = 0; k < n_wave; ++k) {
    audio[static_cast<size_t>(n_lead + k)] = std::sin(phi);
    phi = std::fmod(phi + dphi[static_cast<size_t>(k + n_spsym)], 2.0f * kPi);
  }
  const int n_ramp = n_spsym / 8;
  for (int i = 0; i < n_ramp; ++i) {
    const float env =
        (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / (2.0f * n_ramp))) / 2.0f;
    audio[static_cast<size_t>(n_lead + i)] *= env;
    audio[static_cast<size_t>(n_lead + n_wave - 1 - i)] *= env;
  }
  return audio;
}

}  // namespace radio::ft8
