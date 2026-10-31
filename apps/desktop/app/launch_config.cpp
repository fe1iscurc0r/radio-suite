// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app/launch_config.h"

#include <QDebug>
#include <QSettings>

#include "core/transport.h"
#include "core/transport_mock.h"
#ifdef RADIO_SUITE_HAVE_UDP
#include "core/transport_pybridge.h"
#include "core/transport_udp.h"
#endif

namespace radio::app {
namespace {

constexpr const char* kMode = "link/mode";
constexpr const char* kHost = "link/host";
constexpr const char* kUser = "link/user";
constexpr const char* kPass = "link/pass";
constexpr const char* kPython = "link/python";

QSettings openSettings() {
#ifdef Q_OS_ANDROID
  // 默认构造走 NativeFormat，Android 上没有对应后端；不显式指定就是「存了没存」
  return QSettings(QSettings::IniFormat, QSettings::UserScope,
                   QStringLiteral("radio-suite"), QStringLiteral("radio-suite"));
#else
  return QSettings();
#endif
}

// 把「想要的链路」折成「实际会建出来的链路」；见 resolveLink 的注释
LinkPrefs sanitize(LinkPrefs p) {
  if (p.link == Link::Mock) return p;
  if (!udpLinkBuilt() || p.host.empty()) {
    p = LinkPrefs{};
    return p;
  }
  if (p.link == Link::PyBridge && !bridgeAvailable()) p.link = Link::Udp;
  return p;
}

}  // namespace

bool bridgeAvailable() {
#ifdef Q_OS_ANDROID
  return false;  // 设备上没有 python 解释器可拉 rsba1_bridge.py
#else
  return true;
#endif
}

LinkPrefs linkPrefsFromCli(bool mock_set, bool radio_set, bool bridge_set,
                           std::string host, std::string user,
                           std::string pass, std::string python_exe) {
  LinkPrefs p;
  if (radio_set) {
    p.link = bridge_set ? Link::PyBridge : Link::Udp;
    p.host = std::move(host);
    p.user = std::move(user);
    p.pass = std::move(pass);
    p.python_exe = std::move(python_exe);
    p.explicit_cli = true;
  } else if (mock_set) {
    p.link = Link::Mock;
    p.explicit_cli = true;
  }
  return p;
}

LinkPrefs resolveLink(const LinkPrefs& cli, const LinkPrefs& saved) {
  if (cli.explicit_cli) return sanitize(cli);
  if (saved.link != Link::Mock) return sanitize(saved);
  return LinkPrefs{};  // 两边都没指向真机：老实跑 mock
}

LinkPrefs loadLinkPrefs(QSettings& st) {
  LinkPrefs p;
  p.link = static_cast<Link>(st.value(QLatin1String(kMode), 0).toInt());
  p.host = st.value(QLatin1String(kHost)).toString().toStdString();
  p.user = st.value(QLatin1String(kUser)).toString().toStdString();
  p.pass = st.value(QLatin1String(kPass)).toString().toStdString();
  p.python_exe = st.value(QLatin1String(kPython)).toString().toStdString();
  return p;
}

void saveLinkPrefs(QSettings& st, const LinkPrefs& p) {
  st.setValue(QLatin1String(kMode), static_cast<int>(p.link));
  st.setValue(QLatin1String(kHost), QString::fromStdString(p.host));
  st.setValue(QLatin1String(kUser), QString::fromStdString(p.user));
  st.setValue(QLatin1String(kPass), QString::fromStdString(p.pass));
  st.setValue(QLatin1String(kPython), QString::fromStdString(p.python_exe));
  st.sync();
}

LinkPrefs loadLinkPrefs() {
  QSettings st = openSettings();
  return loadLinkPrefs(st);
}

void saveLinkPrefs(const LinkPrefs& p) {
  QSettings st = openSettings();
  saveLinkPrefs(st, p);
}

bool udpLinkBuilt() {
#ifdef RADIO_SUITE_HAVE_UDP
  return true;
#else
  return false;
#endif
}

std::shared_ptr<radio::core::ICivTransport> makeTransport(const LinkPrefs& p) {
#ifdef RADIO_SUITE_HAVE_UDP
  if (p.link == Link::PyBridge && bridgeAvailable()) {
    radio::core::PyBridgeTransport::Config cfg;
    cfg.host = p.host;
    cfg.username = p.user;
    cfg.password = p.pass;
    if (!p.python_exe.empty()) cfg.python_exe = p.python_exe;
    return std::make_shared<radio::core::PyBridgeTransport>(std::move(cfg));
  }
  if (p.link != Link::Mock) {
    // 要的是桥但这台设备没有 python，也落到这条路上（resolveLink 已折过，这里是兜底）
    if (p.host.empty()) {
      qWarning() << "链路选了真机但电台 IP 为空，退回 mock";
    } else {
      radio::core::Rsba1UdpTransport::Config cfg;
      cfg.host = p.host;
      cfg.username = p.user;
      cfg.password = p.pass;
      return std::make_shared<radio::core::Rsba1UdpTransport>(std::move(cfg));
    }
  }
#else
  if (p.link != Link::Mock)
    qWarning() << "本构建未编入真机链路（缺 Qt6::Network），退回 mock";
#endif
  return std::make_shared<radio::core::MockTransport>();
}

const char* linkName(Link link) {
  switch (link) {
    case Link::Udp: return "udp";
    case Link::PyBridge: return "python-bridge";
    case Link::Mock: break;
  }
  return "mock";
}

QString linkStatusText(const LinkPrefs& p) {
  if (p.link == Link::Mock)
    return QStringLiteral("transport: mock · radio: IC-705 sim");
  const QString name = QString::fromLatin1(linkName(p.link));
  const QString host = QString::fromStdString(p.host);
  return host.isEmpty() ? QStringLiteral("transport: %1").arg(name)
                        : QStringLiteral("transport: %1 · %2").arg(name, host);
}

int linkFromModeIndex(int index, bool bridge_available) {
  if (index <= 0) return static_cast<int>(Link::Mock);
  if (index == 1) return static_cast<int>(Link::Udp);
  return static_cast<int>(bridge_available ? Link::PyBridge : Link::Udp);
}

int modeIndexFromLink(Link link, bool bridge_available) {
  if (link == Link::Mock) return 0;
  if (link == Link::Udp) return 1;
  return bridge_available ? 2 : 1;  // 没有 Python 桥的平台上按 UDP 显示
}

}  // namespace radio::app
