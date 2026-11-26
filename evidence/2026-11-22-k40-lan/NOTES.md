# 2026-11-22 · K40 真机首轮 LAN 假电台（A-1.5 / A-1.8 / 卫星页）

## 装置

- 手机：Redmi K40（M2012K11AC），Android 13，1080×2400，density 440。
- 网络：PC 开移动热点，PC=192.168.137.1，手机=192.168.137.177，同 /24。
- APK：本仓 `320c3f5`（W2.4 分支头，= 合入 #74 的内容）交叉编译后签名，
  `md5 985f17c74d5d0a5450229c2c278c588e`；设备上 `base.apk` 复核为同一 md5。
- 假电台：`fake_rig.exe --bind 0.0.0.0 --i-know-this-is-lan --scenario nominal`，
  PC 防火墙入站 UDP 50001-50003 已对 fake_rig.exe 放行（规则名 `radio-suite fake_rig UDP`）。
- 应用链路：`radio-suite.ini` 里 `[link] mode=1 host=192.168.137.1`（经 su 写入应用私有目录）。

## 判据与结果

| 判据 | 结果 |
|---|---|
| A-1.3 三信道固定端口在听 | ✅ `netstat` 见 0.0.0.0:50001/50002/50003 |
| A-1.5 手机↔PC 建链 | ✅ 假电台退出小结 **`serial open: yes`**、CI-V 上行 **7648 帧**、disconnects 0（60 s 窗口） |
| A-1.5 状态回读 | ❌ 界面仍 `0.000000 MHz`、S 表 `-999 dBm`、制式 `USB/0`——建流后回读没有上屏（见下「未决」） |
| 卫星页存在 | ✅ 手机第 6 页签「卫星」出现（`02-link-nominal.png`） |
| A-1.8 APK 排除假电台 | ✅ `unzip -l` 里 `fake_rig`/`fake_rsba1` 条目 **0**；`sqldrivers` 1（SQLite 驱动仍在） |

截图：`02-link-nominal.png`（六页签 + 状态栏 `transport: udp · 192.168.137.1`）。
假电台小结原文：`fake_rig-summary.txt`。

## 未决：建流成功但状态不刷新

同一条代码在桌面 in-process 环回（`tests/test_rsba1_loopback.cpp`）是通的，
过真网络后表现为「serial 建流成功、CI-V 有上行帧、界面无任何电台状态」。
还没定因，候选方向（按可疑度）：

1. 回包端口/源地址：fake_rig 从 50002 回给手机源端口，安卓侧是否按连接五元组收；
2. 回包解析：CI-V 回帧在桌面 loopback 里是内存直传，这里是 UDP 真实分片/重排；
3. 会话线程：安卓上回帧进 core 线程后状态是否发到 UI（其它页签有过线程坑）。

下次接上手机要做的：开「面板 → 设备台账」看『回帧活性』行（`rig_reply_age_ms`），
它能把「没收到回包」与「收到了但没上屏」一刀切开。

## 现场踩到的两个坑（已解决，记下来省下次）

- **装错包**：21:46 第一次 `pm install` 装的是 `/tmp` 里遗留的旧 APK（md5 `91bcb…`），
  与刚构建的 `985f…` 不符，导致先看到「只有 5 个页签、没有卫星」的假结论。
  规矩：**装包前后都比一次设备 `base.apk` 的 md5**，对不上就是没装上。
- **heredoc 写 .bat**：Git Bash 的 heredoc 把 `>nul 2>&1` 改写成了 `>/dev/null`，
  cmd 找不到该路径 → vcvars 未执行 → 编译报 `type_traits` 缺失。批处理一律用写文件方式生成，
  不要用 heredoc。

## 未跑完（设备中途从 USB 掉线，待复接）

- A-1.5 状态回读定因（上面「未决」）。
- A-1.6 `silence-after-N`：断链后持续指示与断链后白屏两问。
- A-2.7 卫星页真机一帧（页签已在，页内渲染与填频未截）。
- N4 灭屏收包（WifiLock 单独够不够）。
- L4 §7.2 三条定因实验（缝坐标 / 脏矩形打点 / 整窗 dirty 判别）。