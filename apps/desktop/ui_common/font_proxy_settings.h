// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · ui_common/font_proxy_settings.h — 字体设置 + 代理设置（对齐原版两件）
// 字体：原版 FontManager（默认 Microsoft YaHei，全局字体/字号，QSettings 持久）
// 代理：原版 ProxyManager（HTTP/HTTPS/SOCKS5 → 环境变量；本仓等价物 =
//       QNetworkProxy::setApplicationProxy，作用于 LoTW 上传等 QNAM 请求）
#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;
class QLineEdit;

namespace radio::ui {

// ---- 字体 ----
class FontSettingsDialog : public QDialog {
  Q_OBJECT
 public:
  explicit FontSettingsDialog(QWidget* parent = nullptr);
  // 启动时应用持久化字体（main 调用）
  static void applySaved();

 private slots:
  void onApply();

 private:
  QComboBox* family_ = nullptr;
  QSpinBox* size_ = nullptr;
};

// ---- 代理 ----
class ProxySettingsDialog : public QDialog {
  Q_OBJECT
 public:
  explicit ProxySettingsDialog(QWidget* parent = nullptr);
  // 启动时应用持久化代理（main 调用）
  static void applySaved();

 private slots:
  void onApply();

 private:
  QComboBox* type_ = nullptr;   // 无 / HTTP / SOCKS5
  QLineEdit* host_ = nullptr;
  QSpinBox* port_ = nullptr;
};
}  // namespace radio::ui
