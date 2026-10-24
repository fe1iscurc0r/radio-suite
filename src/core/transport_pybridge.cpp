// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/transport_pybridge.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QCoreApplication>

namespace radio::core {

PyBridgeTransport::PyBridgeTransport(Config cfg) : cfg_(std::move(cfg)) {}

PyBridgeTransport::~PyBridgeTransport() { close(); }

std::string PyBridgeTransport::findBridgeScript() {
  // applicationDirPath 向上找 tools/rsba1_bridge.py：
  //   build/src/app/ → 上 3 级；packaging/out-win/ → 上 2 级
  QDir d(QCoreApplication::applicationDirPath());
  for (int i = 0; i < 4; ++i) {
    const QString cand = d.absoluteFilePath("tools/rsba1_bridge.py");
    if (QFile::exists(cand)) return cand.toStdString();
    if (!d.cdUp()) break;
  }
  return {};
}

bool PyBridgeTransport::open(std::string* error) {
  if (open_) return true;
  const std::string bridge = findBridgeScript();
  if (bridge.empty()) {
    if (error) *error = "找不到 tools/rsba1_bridge.py（须在仓库内运行）";
    return false;
  }
  proc_ = new QProcess();
  proc_->setWorkingDirectory(QCoreApplication::applicationDirPath());
  proc_->start(QString::fromStdString(cfg_.python_exe),
               {QString::fromStdString(bridge),
                QString::fromStdString(cfg_.host),
                QString::fromStdString(cfg_.username),
                QString::fromStdString(cfg_.password)});
  if (!proc_->waitForStarted(5000)) {
    if (error) *error = "python 进程启动失败（检查 PATH/PYTHON 指向真身）";
    close();
    return false;
  }
  sendJson(QString("{\"id\":0,\"cmd\":\"open\"}").toStdString());

  // 等 open 应答（真机建链含重试，放宽到 15s）
  const int64_t deadline = QDateTime::currentMSecsSinceEpoch() + cfg_.open_timeout_ms;
  while (QDateTime::currentMSecsSinceEpoch() < deadline) {
    proc_->waitForReadyRead(200);
    drainStdout();
    if (open_) return true;
    if (proc_->state() != QProcess::Running) break;
  }
  const QString errOut = proc_->readAllStandardError();
  if (error)
    *error = "桥建链超时/失败" +
             (errOut.isEmpty() ? std::string()
                               : ("；stderr: " + errOut.toStdString()));
  close();
  return false;
}

void PyBridgeTransport::sendJson(const std::string& line) {
  if (!proc_) return;
  proc_->write(QString::fromStdString(line).append("\n").toUtf8());
}

void PyBridgeTransport::drainStdout() {
  while (proc_ && proc_->canReadLine()) {
    const QByteArray raw = proc_->readLine().trimmed();
    if (raw.isEmpty()) continue;
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) continue;
    const QJsonObject obj = doc.object();
    if (obj.value("ok").toBool() && obj.contains("hex")) {
      const std::string hex = obj.value("hex").toString().toStdString();
      std::vector<uint8_t> bytes;
      bytes.reserve(hex.size() / 2);
      for (size_t i = 0; i + 1 < hex.size(); i += 2)
        bytes.push_back(static_cast<uint8_t>(
            std::stoul(hex.substr(i, 2), nullptr, 16)));
      if (!bytes.empty()) {
        emitFrame(bytes);
        ++delivered_;
      }
    }
    if (obj.value("id").toVariant().toULongLong() == 0 && obj.value("ok").toBool())
      open_ = true;
  }
}

bool PyBridgeTransport::isOpen() const { return open_ && proc_ &&
    proc_->state() == QProcess::Running; }

bool PyBridgeTransport::send(std::span<const uint8_t> frame) {
  if (!isOpen()) return false;
  std::string hex;
  hex.reserve(frame.size() * 2);
  static const char* kD = "0123456789abcdef";
  for (uint8_t b : frame) {
    hex.push_back(kD[b >> 4]);
    hex.push_back(kD[b & 0xF]);
  }
  pending_civ_id_ = next_id_++;
  sendJson("{\"id\":" + std::to_string(pending_civ_id_) +
           ",\"cmd\":\"civ\",\"hex\":\"" + hex + "\"}");
  return true;
}

size_t PyBridgeTransport::poll() {
  if (!proc_) return 0;
  proc_->waitForReadyRead(0);
  drainStdout();
  const size_t n = delivered_;
  delivered_ = 0;
  return n;
}

void PyBridgeTransport::close() {
  if (proc_ && proc_->state() == QProcess::Running) {
    sendJson("{\"id\":999,\"cmd\":\"close\"}");
    proc_->waitForFinished(3000);
  }
  if (proc_) {
    proc_->kill();
    delete proc_;
    proc_ = nullptr;
  }
  open_ = false;
}

}  // namespace radio::core
