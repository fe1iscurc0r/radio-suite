# SPDX-License-Identifier: AGPL-3.0-or-later

## packaging

三平台打包脚本在 **M5** 落地：

- Windows：`windeployqt` + Inno Setup/MSI
- Linux：`linuxdeploy` → AppImage
- macOS：`macdeployqt` → dmg

注意 Qt 走 LGPLv3 动态链接合规路径（详见根目录 NOTICE.md）；体积数据记录见
docs/adr/。
