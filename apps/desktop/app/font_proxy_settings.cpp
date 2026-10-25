// SPDX-License-Identifier: AGPL-3.0-or-later
// 字体/代理设置实现——对齐原版 FontManager 与 ProxyManager 语义（本地 HamLog R1.0.0）
#include "app/font_proxy_settings.h"

#include <QApplication>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkProxy>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace radio::app {
namespace {
constexpr const char* kOrg = "radio-suite";
}  // namespace

// ---- 字体 ----

FontSettingsDialog::FontSettingsDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("字体设置");
  auto* root = new QVBoxLayout(this);
  auto* form = new QFormLayout();
  family_ = new QComboBox(this);
  family_->addItems(QFontDatabase::families());
  const QFont cur = QApplication::font();
  const int fi = family_->findText(cur.family());
  family_->setCurrentIndex(fi < 0 ? family_->findText("Microsoft YaHei") : fi);
  form->addRow("字体族:", family_);
  size_ = new QSpinBox(this);
  size_->setRange(8, 24);
  size_->setValue(cur.pointSize() > 0 ? cur.pointSize() : 10);
  form->addRow("字号:", size_);
  root->addLayout(form);

  auto* row = new QHBoxLayout();
  row->addStretch(1);
  auto* apply = new QPushButton("应用并保存", this);
  connect(apply, &QPushButton::clicked, this, &FontSettingsDialog::onApply);
  row->addWidget(apply);
  auto* reset = new QPushButton("恢复默认", this);
  connect(reset, &QPushButton::clicked, this, [this] {
    QApplication::setFont(QFont("Microsoft YaHei", 10));
    QSettings st(kOrg, "radio-suite");
    st.remove("font");
    accept();
  });
  row->addWidget(reset);
  root->addLayout(row);
}

void FontSettingsDialog::applySaved() {
  QSettings st(kOrg, "radio-suite");
  const QString family = st.value("font/family").toString();
  const int size = st.value("font/size", 10).toInt();
  if (!family.isEmpty() && size > 0)
    QApplication::setFont(QFont(family, size));
}

void FontSettingsDialog::onApply() {
  QApplication::setFont(QFont(family_->currentText(), size_->value()));
  QSettings st(kOrg, "radio-suite");
  st.setValue("font/family", family_->currentText());
  st.setValue("font/size", size_->value());
  accept();
}

// ---- 代理 ----

ProxySettingsDialog::ProxySettingsDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("代理设置（作用于 LoTW 上传等网络请求）");
  auto* root = new QVBoxLayout(this);
  auto* form = new QFormLayout();
  type_ = new QComboBox(this);
  type_->addItem("无（直连）");
  type_->addItem("HTTP");
  type_->addItem("SOCKS5");
  form->addRow("类型:", type_);
  host_ = new QLineEdit(this);
  host_->setPlaceholderText("如 127.0.0.1");
  form->addRow("主机:", host_);
  port_ = new QSpinBox(this);
  port_->setRange(1, 65535);
  port_->setValue(7890);
  form->addRow("端口:", port_);
  root->addLayout(form);

  QSettings st(kOrg, "radio-suite");
  type_->setCurrentIndex(st.value("proxy/type", 0).toInt());
  host_->setText(st.value("proxy/host").toString());
  port_->setValue(st.value("proxy/port", 7890).toInt());

  auto* row = new QHBoxLayout();
  row->addStretch(1);
  auto* apply = new QPushButton("应用并保存", this);
  connect(apply, &QPushButton::clicked, this, &ProxySettingsDialog::onApply);
  row->addWidget(apply);
  root->addLayout(row);
}

void ProxySettingsDialog::applySaved() {
  QSettings st(kOrg, "radio-suite");
  const int type = st.value("proxy/type", 0).toInt();
  if (type == 0) {
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    return;
  }
  QNetworkProxy p(type == 1 ? QNetworkProxy::HttpProxy
                            : QNetworkProxy::Socks5Proxy,
                  st.value("proxy/host").toString(),
                  static_cast<quint16>(st.value("proxy/port", 7890).toUInt()));
  QNetworkProxy::setApplicationProxy(p);
}

void ProxySettingsDialog::onApply() {
  QSettings st(kOrg, "radio-suite");
  st.setValue("proxy/type", type_->currentIndex());
  st.setValue("proxy/host", host_->text());
  st.setValue("proxy/port", port_->value());
  applySaved();
  accept();
}

}  // namespace radio::app
