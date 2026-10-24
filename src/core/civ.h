// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · core/civ.h — Icom CI-V 帧编解码
//
// 事实基准（勿凭记忆改协议）：
//   - vendor/rsba1-core/src/rsba1/ctypes_wrappers/civ_commands.py（MIT 参考实现）
//   - docs/真机验证报告.md（IC-705 端到端实测，2026-08-18）
// 帧格式：[FE × n][to][from][cmd (+subcmd+data)][FD]，n 标准 = 2。
// 真机修正记录：0x05=设频率 / 0x06=设模式（曾与直觉写反，2026-08-18 真机确认）。
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace radio::core::civ {

// ---- 帧定界与地址 ----
inline constexpr uint8_t kPreamble = 0xFE;   // 前导字节（重复 n 次）
inline constexpr uint8_t kPostamble = 0xFD;  // 帧结束
inline constexpr int kDefaultPreambleCount = 2;
inline constexpr uint8_t kIc705Addr = 0xA4;             // IC-705 CI-V 地址
inline constexpr uint8_t kIc7300Addr = 0x04;            // IC-7300
inline constexpr uint8_t kDefaultControllerAddr = 0xE0; // 经典控制器地址（工单帧示例 FE FE A4 E0 03 FD）
// 注：RS-BA1 V2 网络路径控制器地址用 0x00（见参考实现 FROM_ADDR）；两者解析层均接受。

// ---- 命令码 ----
inline constexpr uint8_t kCmdReadFreq = 0x03;   // 读频率（应答回 5 字节 BCD）
inline constexpr uint8_t kCmdReadMode = 0x04;   // 读模式（应答回 [mode, filter]）
inline constexpr uint8_t kCmdSetFreq = 0x05;    // 设频率（携带 5 字节 BCD）
inline constexpr uint8_t kCmdSetMode = 0x06;    // 设模式（携带 [mode, filter]）
inline constexpr uint8_t kCmdReadPtt = 0x14;    // 读 PTT 状态（子命令 0x0C）
inline constexpr uint8_t kCmdReadSmeter = 0x1A; // 读 S-meter（子命令 0x03）
inline constexpr uint8_t kCmdPtt = 0x1C;        // PTT 控制（子命令 0x00，数据 01=TX/00=RX）
inline constexpr uint8_t kCmdAtt = 0x11;        // ATT 衰减（0x00=OFF / 0x20=20dB，仅 HF/50MHz）
inline constexpr uint8_t kCmdSw16 = 0x16;       // 0x16 子命令开关族（见 kSub16*）
inline constexpr uint8_t kAck = 0xFB;           // 设置类命令成功应答
inline constexpr uint8_t kNak = 0xFA;           // 设置类命令拒绝应答

// 0x16 子命令（radio_link.py:778-835，真机验证）
inline constexpr uint8_t kSub16Pamp = 0x02;      // 前置放大 0/1/2
inline constexpr uint8_t kSub16Agc = 0x12;       // AGC 1=fast/2=mid/3=slow
inline constexpr uint8_t kSub16Nb = 0x22;        // 噪声抑制 NB 0/1
inline constexpr uint8_t kSub16Nr = 0x40;        // NR 降噪 0/1
inline constexpr uint8_t kSub16NotchAuto = 0x41; // Auto Notch 0/1
inline constexpr uint8_t kSub16Moni = 0x45;      // MONI 监听 0/1
inline constexpr uint8_t kSub16Vox = 0x46;       // VOX 声控 0/1
inline constexpr uint8_t kSub16NotchMan = 0x48;  // Manual Notch 0/1

// ---- 模式表（radio_link.py:545）----
enum class Mode : uint8_t {
  LSB = 0, USB = 1, AM = 2, CW = 3, RTTY = 4, FM = 5, WFM = 6,
};
const char* modeName(Mode m);
std::optional<Mode> modeFromCode(uint8_t code);

// S-meter 标度（radio_link.py:683）：原始值 0000=S0，0120=S9，0241=S9+60dB。
// 上界按线性外推；>255 未定义。
float smeterToDbm(int raw);
inline float smeterSUnits(int raw) { return raw * 9.0f / 120.0f; }

// ---- 帧 ----
struct CivFrame {
  uint8_t to = 0;
  uint8_t from = 0;
  uint8_t cmd = 0;
  std::vector<uint8_t> payload;  // 子命令 + 数据（不含 cmd / FE / FD）
};

// 构造完整帧：[FE×n][to][from][body][FD]
std::vector<uint8_t> buildFrame(uint8_t to, uint8_t from,
                                std::span<const uint8_t> body,
                                int preamble_count = kDefaultPreambleCount);

// 解析帧。容错策略与参考实现 parse_frame 一致：
// 首字节必须 FE、末字节必须 FD、跳过全部前导 FE、前导后至少 to/from/cmd 三字节。
// 任何不满足返回 std::nullopt（线上数据可能是垃圾，不走异常）。
std::optional<CivFrame> parseFrame(std::span<const uint8_t> data);

// ---- 频率 BCD（5 字节、LSB-first）----
// 例：14270000 Hz -> {00 00 70 42 01}（hex 字面）。
std::optional<std::array<uint8_t, 5>> freqToBcd5(uint64_t hz);       // 超出 10 位 BCD 范围失败
std::optional<uint64_t> bcdToFreq(std::span<const uint8_t> bcd);     // 1..5 字节；半字节 >9 失败

// ---- 常用命令帧构造 ----
std::vector<uint8_t> readFreqFrame(uint8_t to = kIc705Addr,
                                   uint8_t from = kDefaultControllerAddr);
std::vector<uint8_t> setFreqFrame(uint64_t hz,
                                  uint8_t to = kIc705Addr,
                                  uint8_t from = kDefaultControllerAddr);  // 频率越界仍构造（门禁在上层）
std::vector<uint8_t> readModeFrame(uint8_t to = kIc705Addr,
                                   uint8_t from = kDefaultControllerAddr);
std::vector<uint8_t> setModeFrame(Mode mode, int filter = 0,
                                  uint8_t to = kIc705Addr,
                                  uint8_t from = kDefaultControllerAddr);
std::vector<uint8_t> pttFrame(bool tx,
                              uint8_t to = kIc705Addr,
                              uint8_t from = kDefaultControllerAddr);
std::vector<uint8_t> readPttFrame(uint8_t to = kIc705Addr,
                                  uint8_t from = kDefaultControllerAddr);
std::vector<uint8_t> readSmeterFrame(uint8_t to = kIc705Addr,
                                     uint8_t from = kDefaultControllerAddr);
// ATT（IC-705 语义：0x00=OFF，0x20=20dB；电台侧仅 HF/50MHz 接受，其余 NAK）
std::vector<uint8_t> attFrame(bool on_20db,
                              uint8_t to = kIc705Addr,
                              uint8_t from = kDefaultControllerAddr);
std::vector<uint8_t> readAttFrame(uint8_t to = kIc705Addr,
                                  uint8_t from = kDefaultControllerAddr);
// 0x16 子命令开关族：设 / 读
std::vector<uint8_t> sw16SetFrame(uint8_t sub, uint8_t value,
                                  uint8_t to = kIc705Addr,
                                  uint8_t from = kDefaultControllerAddr);
std::vector<uint8_t> sw16ReadFrame(uint8_t sub,
                                   uint8_t to = kIc705Addr,
                                   uint8_t from = kDefaultControllerAddr);

// ---- 应答解析（输入 parseFrame 的产物；cmd/子命令/长度不符返回 nullopt）----
std::optional<uint64_t> parseFreqResponse(const CivFrame& f);   // cmd=0x03 + 5B BCD
struct ModeInfo {
  Mode mode = Mode::USB;
  int filter = 0;
};
std::optional<ModeInfo> parseModeResponse(const CivFrame& f);   // cmd=0x04 + [mode,filter]
std::optional<int> parseSmeterResponse(const CivFrame& f);      // cmd=0x1A + [0x03, raw]
std::optional<bool> parsePttResponse(const CivFrame& f);        // cmd=0x14 + [0x0C, 0/1]
std::optional<int> parseAttResponse(const CivFrame& f);         // cmd=0x11 + [val]
std::optional<int> parseSw16Response(const CivFrame& f, uint8_t sub);  // cmd=0x16 + [sub, val]
inline bool isAck(const CivFrame& f) { return f.cmd == kAck; }
inline bool isNak(const CivFrame& f) { return f.cmd == kNak; }

}  // namespace radio::core::civ
