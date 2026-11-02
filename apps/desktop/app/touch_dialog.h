// SPDX-License-Identifier: AGPL-3.0-or-later
// 手机触摸适配：对话框与独立面板没有系统标题栏/关闭按钮，尺寸也不受屏幕约束
#pragma once

#include <QString>

class QDialog;
class QWidget;

namespace radio::app::touch {

// 关掉 Qt widgets 的「快移动 / 快滚动」：那条快路径滚动时只搬像素、只补画
// 新露出来的窄条，留在画面里的行一次都不重画。真机甩动台账时它留下屏幕左端
// 一条慢一排的竖直滞后带（缝在 x≈148）。必须在任何控件绘制之前调用。
void installScrollRepaintWorkaround();

// 安卓：内容装进滚动区、底部钉一条「关闭」、窗口压进屏幕可用区域。
// 其它平台空操作，桌面窗口管理器照旧给标题栏和 X。
void fitDialog(QDialog* dlg);

// 安卓：给独立顶层面板插一条「标题 + 关闭」顶栏（关=hide，返回关闭按钮）。
// 桌面返回 nullptr —— 有窗口管理器，不需要。
QWidget* addCloseBar(QWidget* panel, const QString& title);

// QMessageBox 在手机上同样没有标题栏，长文案会把唯一按钮挤出屏幕；
// 用可滚动 + 常驻关闭的等价信息框替代
void showInfo(QWidget* parent, const QString& title, const QString& text);

// 安卓：Qt 的顶层窗口触摸换算按窗口位置（÷dpr）偏移，下拉弹窗跟在控件
// 旁边位置必非零，选项永远点不准；应用启动时装一次，把弹窗钉到 (0,0)。
// 其它平台空操作。
void installPopupOriginFix();

}  // namespace radio::app::touch
