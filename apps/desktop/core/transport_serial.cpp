// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/transport_serial.h"

#include <cstring>

#include "core/civ.h"

namespace radio::core {

#ifndef RADIO_SUITE_HAVE_SERIAL_QT
// 套件没有 Qt6::SerialPort（CI 的 Ubuntu 就是这样）时，本文件仍然要能编译链接：
// 帧定界那套逻辑跟串口后端无关，用例靠注入的内存管道跑。真后端在
// transport_serial_qt.cpp 里，那个 TU 只在找到 SerialPort 时才参与构建。
std::unique_ptr<ICivBytePipe> makeSerialPipe(const std::string& /*port_name*/,
                                            int /*baud*/) {
  return nullptr;
}
#endif

SerialCivTransport::SerialCivTransport(Config cfg)
    : SerialCivTransport(cfg, makeSerialPipe(cfg.port_name, cfg.baud)) {
  // 空名与「没编入后端」都会让工厂给出 nullptr，但原因不同，日志不能混为一谈
  if (!pipe_ && !cfg_.port_name.empty())
    log_ = "no serial backend in this build (Qt6::SerialPort missing)\n";
}

SerialCivTransport::SerialCivTransport(Config cfg, std::unique_ptr<ICivBytePipe> pipe)
    : cfg_(std::move(cfg)), pipe_(std::move(pipe)) {}

SerialCivTransport::~SerialCivTransport() { close(); }

bool SerialCivTransport::open(std::string* error) {
  rx_.clear();
  if (!pipe_) {
    if (error) {
      // 报错了原因，用户会去白装一遍 QtSerialPort，而真正的问题只是没给口名
      *error = cfg_.port_name.empty()
                   ? std::string("串口名为空：--serial 要给 COM3 或 /dev/ttyUSB0")
                   : "串口 " + cfg_.port_name +
                         " 打不开：本构建没编入串口后端（缺 Qt6::SerialPort）";
    }
    return false;
  }
  if (!pipe_->open(error)) return false;
  log_ += "opened " + cfg_.port_name + " @" + std::to_string(cfg_.baud) + " 8N1\n";
  return true;
}

void SerialCivTransport::close() {
  if (!pipe_) return;
  pipe_->close();
  rx_.clear();
}

bool SerialCivTransport::isOpen() const {
  return pipe_ && pipe_->isOpen();
}

bool SerialCivTransport::send(std::span<const uint8_t> frame) {
  if (!isOpen() || frame.empty()) return false;
  if (!pipe_->write(frame)) {
    log_ += "write failed (" + std::to_string(frame.size()) + " bytes)\n";
    return false;
  }
  return true;
}

size_t SerialCivTransport::poll() {
  if (!isOpen()) return 0;
  size_t frames = 0;
  // readAvailable() 是「取走」而不是「 peek」，所以一次 poll 里只能读一遍：
  // 读到空就收工，剩下的交给下一轮（session 250 ms / 网关 pump 都在反复叫 poll）。
  // guard 只是防呆，正常路径第一遍就空了。
  for (int guard = 0; guard < 64; ++guard) {
    const QByteArray chunk = pipe_->readAvailable();
    if (chunk.isEmpty()) break;
    rx_.append(chunk);
    frames += parseBuffer();
  }
  return frames;
}

size_t SerialCivTransport::parseBuffer() {
  const std::span<const uint8_t> buf(
      reinterpret_cast<const uint8_t*>(rx_.constData()),
      static_cast<size_t>(rx_.size()));
  size_t frames = 0;
  const size_t consumed = civ::extractFrames(
      buf,
      [this, &frames](std::span<const uint8_t> f) {
        emitFrame(f);
        ++frames;
        ++delivered_;
      },
      &discarded_);
  rx_.remove(0, static_cast<qint64>(consumed));
  if (static_cast<size_t>(rx_.size()) > civ::kMaxPendingStreamBytes) {
    discarded_ += static_cast<size_t>(rx_.size());
    log_ += "drop " + std::to_string(rx_.size()) +
            " bytes without terminator (stream out of sync)\n";
    rx_.clear();
  }
  return frames;
}

}  // namespace radio::core
