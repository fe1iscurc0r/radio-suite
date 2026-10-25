# rsba1-core 合作工作流（双仓关系）

- 定位（师傅 2026-10-25 拍板）：**rsba1-core（MIT）与 radio-suite（AGPL-3.0）不完全
  重合**。rsba1-core 是独立 MIT 仓，**准备与上游合作方在那边开发**；radio-suite 把
  它直接放进来（`libs/protocol/rsba1-core/`）作为**活的新项目**，不是死快照。

## 双仓关系图

```
fe1iscurc0r/rsba1-core（MIT，合作开发在上游进行）
        │  git subtree pull（定期同步）
        ▼
radio-suite/libs/protocol/rsba1-core/（AGPL 仓内的 MIT 子项目）
        │  ①fixtures 生成（tests/fixtures/gen_from_reference.py）
        │  ②Python 桥直跑（tools/rsba1_bridge.py / transport_pybridge）
        │  ③C++ 协议层对照（rsba1_proto.cpp 与它字节级互锁）
        ▼
radio-suite 桌面端 / 总线 / agent 生态
```

## 同步命令

```bash
# 上游 → 本仓（合作成果进入 radio-suite）
git subtree pull --prefix=libs/protocol/rsba1-core \
  https://github.com/fe1iscurc0r/rsba1-core main --squash

# 同步后必做：fixtures 再生成 + 全量测试（互锁会立即暴露协议漂移）
python tests/fixtures/gen_from_reference.py
cmake --build build && ctest --test-dir build --output-on-failure
```

首次把既有快照转为 subtree 跟踪时（本仓当前状态=快照），下一次 pull 用
`--squash` 会把上游历史作为一次合并提交接入——若上游 main 与快照同源无分叉，
pull 即 no-op；有分叉则按 subtree 合并语义处理。

## 许可边界（写死）

1. **方向性**：MIT（rsba1-core）→ AGPL（radio-suite）**顺流合法**（MIT 允许任意
   再许可，保留版权行即可）；**逆流**（radio-suite 的 AGPL 代码进 rsba1-core）
   必须由版权人显式以 MIT 双许可发布，默认禁止。
2. **合作开发的贡献落在上游 MIT 仓**——外部合作者不受本仓 AGPL 传染；
   radio-suite 侧对 rsba1-core 的修改**不上游回推**（保持上游纯 MIT），
   除非修改者本人即版权人并愿意 MIT 放出。
3. 本仓对 rsba1-core 的**封装层**（tools/rsba1_bridge.py、C++ 移植件
   rsba1_proto.cpp / audio_stream.cpp）均为 AGPL/标注移植，不属于 rsba1-core。

## 各消费点（改 rsba1-core 后需回归的清单）

| 消费点 | 路径 | 回归方式 |
|---|---|---|
| fixtures 互锁 | tests/fixtures/*.hex | `python tests/fixtures/gen_from_reference.py` + ctest |
| Python 桥 | tools/rsba1_bridge.py（MCP 直跑入口） | `python tools/rsba1_bridge.py`（usage 输出=import 链 OK） |
| C++ 协议对照 | apps/desktop/core/rsba1_proto.cpp | ctest `[rsba1]` 标签 |
| 文档引用 | docs/WO-274-spike.md 等 | grep vendor/rsba1-core 应为 0（libs/protocol 取代） |
