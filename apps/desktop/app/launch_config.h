// SPDX-License-Identifier: AGPL-3.0-or-later
// radio-suite · app/launch_config.h — 链路选择与 transport 工厂
//
// 为什么要单独立一个文件：传输层原先只在 main.cpp 里按命令行开关构造，而 Android
// 没有命令行输入口（「连接…」对话框只渲染一行命令文本，自己不建任何传输），
// 装到手机上的包因此**永远只能跑 mock**。这里把「选哪条链路」抽成纯函数
// + 一个工厂，优先级：命令行 > 持久化偏好（Android 唯一通路）> mock。
#pragma once

#include <memory>
#include <string>

#include <QString>

class QSettings;

namespace radio::core {
class ICivTransport;
}

namespace radio::app {

enum class Link : int { Mock = 0, Udp = 1, PyBridge = 2 };

struct LinkPrefs {
  Link link = Link::Mock;
  // 命令行是否真的表过态（给了 --radio 或 --mock）。没表态才让持久化偏好生效，
  // 否则桌面上「上次连过的电台」会把这次的 --mock 吃掉。
  bool explicit_cli = false;
  std::string host;
  std::string user;
  std::string pass;
  std::string python_exe;  // 空 = 用 PATH 上的 python
};

// 命令行 → 偏好。纯函数，便于把优先级钉在用例里。
LinkPrefs linkPrefsFromCli(bool mock_set, bool radio_set, bool bridge_set,
                           std::string host, std::string user,
                           std::string pass, std::string python_exe);

// cli 表过态就用 cli，否则用 saved。返回值是**这条构建实际会建出来的链路**：
// 没编入 UDP、电台 IP 为空、平台上没有 Python 桥，都在这里落回真话，
// 所以状态栏文案和工厂用的是同一个判定，不会再各说各的。
LinkPrefs resolveLink(const LinkPrefs& cli, const LinkPrefs& saved);

// 持久化偏好。Android 的 QSettings 默认格式（NativeFormat）没有后端，
// 这里显式走 IniFormat，否则「连接…」里存了等于没存。
LinkPrefs loadLinkPrefs();
void saveLinkPrefs(const LinkPrefs&);
// 显式后端的两个重载给用例用（临时 ini 文件），生产代码走上面两个。
LinkPrefs loadLinkPrefs(QSettings& st);
void saveLinkPrefs(QSettings& st, const LinkPrefs&);

// 这个构建里有没有编入真机链路（缺 Qt6::Network 就没有，只能退回 mock）。
bool udpLinkBuilt();

// 这台设备能不能用 Python 桥（Android 上没有 python 解释器可拉 rsba1_bridge.py）。
bool bridgeAvailable();

// 唯一的 transport 工厂：GUI 与 headless 两条启动路共用，别再各写一遍 host/user/pass。
// 入参取 resolveLink 的输出（那是全应用唯一决定链路的入口）。
std::shared_ptr<radio::core::ICivTransport> makeTransport(const LinkPrefs&);

const char* linkName(Link);

// 状态栏的空闲文本。原来硬编码 "transport: mock"，真连上 UDP 也这么写 —— 这条函数
// 让文本跟着 LinkPrefs 走，「链路是什么」才重新有可信的出处。
QString linkStatusText(const LinkPrefs&);

// 对话框下拉框序号 ↔ Link。没有 Python 桥的平台上只有两档（mock/UDP），
// 序号与枚举的映射放一处才不会和对话框各写一遍。
int linkFromModeIndex(int index, bool bridge_available);
int modeIndexFromLink(Link link, bool bridge_available);
}  // namespace radio::app
