# SPDX-License-Identifier: AGPL-3.0-or-later

## apps/desktop/logbook

HamLog 融合模块（QtSql/QSQLITE）：与 HamLog 共用 `Log.db`，提供 QSO 新增、
四字段模糊搜索、完整编辑、删除、设置、ADIF 导出和 LoTW 上传对话框。

数据层 `HamLogDb` 支持注入数据库路径；自动化测试只使用 `QTemporaryDir` 下的
临时 SQLite，不触碰真实日志库。ADIF 字段长度按 UTF-8 字节数计算。
当前没有 DXCC 前缀表实现。
