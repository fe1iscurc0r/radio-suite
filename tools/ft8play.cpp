// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · tools/ft8play.cpp — 把一整个 FT8 时隙播到声卡出口（台架信号源）
//
// 用途有两件：①不接电台时验证接收链（本机麦克风对着扬声器就能解）；②真机复验时
// 把这路音频灌进电台 MOD，看对方（或本机 50003 回环）能不能解回原文。
// 纯命令行、不依赖 Qt：radio::ft8 渲染 + radio::audio 播放。
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "audio/audio_out.h"
#include "ft8/ft8_tx.h"

namespace {

void usage(const char* argv0) {
  std::printf(
      "用法: %s [--list] [文本] [音频频偏Hz] [峰值0..1] [设备ID]\n"
      "  默认: \"CQ BG5GXO PM01\" 1500 0.70 系统默认输出设备\n"
      "播满一个 15 s FT8 时隙（48 kHz s16 mono），按 UTC 槽起点对齐由调用方负责。\n",
      argv0);
}

}  // namespace

int main(int argc, char** argv) {
  radio::audio::AudioOut out;
  if (argc > 1 && std::string(argv[1]) == "--list") {
    const auto names = out.deviceNames();
    const auto ids = out.outputDeviceIds();
    for (size_t i = 0; i < names.size(); ++i)
      std::printf("%2zu  id=%-4u  %s\n", i, ids.empty() ? 0u : ids[i],
                  names[i].c_str());
    std::printf("默认输出设备 id = %d\n", out.defaultOutputDevice());
    return 0;
  }

  const std::string text = argc > 1 ? argv[1] : "CQ BG5GXO PM01";
  const float f0 = argc > 2 ? std::strtof(argv[2], nullptr) : 1500.0f;
  const float peak = argc > 3 ? std::strtof(argv[3], nullptr) : 0.70f;
  const int dev = argc > 4 ? std::atoi(argv[4]) : -1;

  std::vector<int16_t> pcm;
  try {
    pcm = radio::ft8::renderTxPcm(text, f0, peak);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "渲染失败: %s\n", e.what());
    std::fprintf(stderr, "文本得能按 FT8 打包（自由文本 13 字符）\n");
    return 2;
  }

  int device = dev;
  if (device < 0) device = out.defaultOutputDevice();
  if (device < 0) {
    std::fprintf(stderr, "没有可用的音频输出设备（--list 看看）\n");
    return 3;
  }

  std::printf("播放 \"%s\" · 频偏 %.0f Hz · 峰值 %.2f · 设备 id=%d · 15.0 s\n",
              text.c_str(), f0, peak, device);
  std::fflush(stdout);

  std::string err;
  if (!out.play(pcm, radio::ft8::kTxSampleRateHz,
                static_cast<uint32_t>(device), &err)) {
    std::fprintf(stderr, "播放失败: %s\n", err.c_str());
    return 4;
  }
  std::printf("整槽播完（%zu 样本）\n", pcm.size());
  return 0;
}
