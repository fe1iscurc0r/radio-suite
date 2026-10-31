// SPDX-License-Identifier: AGPL-3.0-or-later
// 手机触摸适配：对话框与独立面板没有系统标题栏/关闭按钮，尺寸也不受屏幕约束
#pragma once

#include <QString>

class QDialog;
class QWidget;

namespace radio::app::touch {

// 安卓：内容装进滚动区、底部钉一条「关闭」、窗口压进屏幕可用区域。
// 其它平台空操作，桌面窗口管理器照旧给标题栏和 X。
void fitDialog(QDialog* dlg);

// 安卓：给独立顶层面板插一条「标题 + 关闭」顶栏（关=hide，返回关闭按钮）。
// 桌面返回 nullptr —— 有窗口管理器，不需要。
QWidget* addCloseBar(QWidget* panel, const QString& title);

// QMessageBox 在手机上同样没有标题栏，长文案会把唯一按钮挤出屏幕；
// 用可滚动 + 常驻关闭的等价信息框替代
void showInfo(QWidget* parent, const QString& title, const QString& text);

}  // namespace radio::app::touch
