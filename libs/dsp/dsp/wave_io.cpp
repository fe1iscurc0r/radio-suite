// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · dsp/wave_io.cpp
#include "dsp/wave_io.h"

#include <array>
#include <cstring>
#include <fstream>

namespace radio::dsp {
namespace {

constexpr uint32_t kHdr = 44;

// WAV 头全小端。putU32/putU16 都是 LE（别照搬协议域的 BE 写法——这里踩过）。
void putU32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b[off + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

void putU16(std::vector<uint8_t>& b, size_t off, uint32_t v) {
  b[off] = static_cast<uint8_t>(v & 0xFF);
  b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

uint32_t getU32(const std::array<uint8_t, 44>& h, size_t off) {
  return h[off] | (h[off + 1] << 8) | (h[off + 2] << 16) |
         (h[off + 3] << 24);
}

uint32_t getU16(const std::array<uint8_t, 44>& h, size_t off) {
  return h[off] | (h[off + 1] << 8);
}

}  // namespace

bool writeWavMono16(const std::string& path, const std::vector<int16_t>& pcm,
                    uint32_t sample_rate, std::string* error) {
  const size_t data_len = pcm.size() * 2;
  std::vector<uint8_t> hdr(kHdr, 0);
  hdr[0] = 'R'; hdr[1] = 'I'; hdr[2] = 'F'; hdr[3] = 'F';
  putU32(hdr, 4, static_cast<uint32_t>(36 + data_len));
  hdr[8] = 'W'; hdr[9] = 'A'; hdr[10] = 'V'; hdr[11] = 'E';
  hdr[12] = 'f'; hdr[13] = 'm'; hdr[14] = 't'; hdr[15] = ' ';
  putU32(hdr, 16, 16);               // fmt size
  putU16(hdr, 20, 1);                // PCM
  putU16(hdr, 22, 1);                // mono
  putU32(hdr, 24, sample_rate);
  putU32(hdr, 28, sample_rate * 2);  // byte rate
  putU16(hdr, 32, 2);                // block align
  putU16(hdr, 34, 16);               // bits
  hdr[36] = 'd'; hdr[37] = 'a'; hdr[38] = 't'; hdr[39] = 'a';
  putU32(hdr, 40, static_cast<uint32_t>(data_len));
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    if (error) *error = "打不开写文件: " + path;
    return false;
  }
  f.write(reinterpret_cast<const char*>(hdr.data()), kHdr);
  f.write(reinterpret_cast<const char*>(pcm.data()),
          static_cast<std::streamsize>(data_len));
  if (!f) {
    if (error) *error = "写数据失败: " + path;
    return false;
  }
  return true;
}

bool readWavMono16(const std::string& path, uint32_t expected_rate,
                   std::vector<int16_t>* pcm, std::string* error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (error) *error = "打不开读文件: " + path;
    return false;
  }
  std::array<uint8_t, 44> h{};
  f.read(reinterpret_cast<char*>(h.data()), kHdr);
  if (!f) {
    if (error) *error = "头不完整";
    return false;
  }
  if (h[0] != 'R' || h[1] != 'I' || h[2] != 'F' || h[3] != 'F' ||
      h[8] != 'W' || h[9] != 'A' || h[10] != 'V' || h[11] != 'E') {
    if (error) *error = "不是 RIFF/WAVE";
    return false;
  }
  // fmt：audio_format(2B@20)=1(PCM)、channels(2B@22)=1、bits(2B@34)=16
  if (getU16(h, 20) != 1 || getU16(h, 22) != 1 || getU16(h, 34) != 16) {
    if (error) *error = "只支持 PCM/s16/mono";
    return false;
  }
  const uint32_t rate = getU32(h, 24);
  if (rate != expected_rate) {
    if (error) *error = "采样率不符: 头里 " + std::to_string(rate) +
                        "，链路按 " + std::to_string(expected_rate);
    return false;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
  pcm->clear();
  pcm->resize(data.size() / 2);
  std::memcpy(pcm->data(), data.data(), pcm->size() * 2);
  return true;
}

}  // namespace radio::dsp
