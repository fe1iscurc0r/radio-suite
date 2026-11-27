# WO-277 · Android OTG 串口后端（CI-V / KISS），2026-11-27 立项

> 起因：台账「USB Host 特性」只报「有硬件但后端没接」。师傅裁决：**接后端**，
> 让这一行从「降级」变「可用」。本单只做安卓侧的 USB 串口后端，桌面 QtSerialPort 路径不动。

## §0 边界

- 目标：手机 OTG 接 USB-CDC / CH340 / CP210x / FTDI 的 CAT 线或 TNC，
  分别供给 `ICivTransport`（CI-V）与 KISS（APRS）。
- 不做：蓝牙 SPP、USB 音频、驱动式内核模块。
- 硬件判据优先：**没有实物就不算验过**（下面 §4 的真机清单为交付门）。

## §1 设计（最小可维护形态）

```
Java 层（packaging/android/src/org/radiosuite/app/UsbSerial.java）
  UsbManager 枚举 → 过滤 CDC(0x02)/CH340(0x1A86)/CP210x(0x10C4)/FTDI(0x0403)
  requestPermission(PendingIntent) → 回调置 ready
  open → claimInterface(0) → bulkIn/bulkOut 端点
  read(timeoutMs): byte[]   write(byte[])   close()
C++ 层（apps/desktop/core/transport_usb_serial.{h,cpp}）
  QJniObject 调 Java；poll() 每拍 read(10ms) 拼进现有帧解析器
  复用 civ::extractFrames / kiss 分帧，不另造协议
Android 接线
  main.cpp：Android 上链路优先级 CLI > 持久化 > 默认；新增 mode=usb-serial
  台账行：ready 且能收帧 → 可用；有设备未授权 → 降级；无设备 → 降级(无硬件)
```

关键取舍：

- **轮询式 JNI**（read(timeout)/write）而不是从 Java 回调 C++：与现有
  `ICivTransport::poll()` 同构，少一层线程与生命周期坑。
- 串口参数：CI-V 默认 9600 8N1（IC-705 出厂），KISS 默认 1200 Bd TNC 侧自理；
  两个参数进设置（`serial/baud`、`serial/dtr_rts`），不写死。
- 权限被拒/拔线：poll 返回错误计数；连续 N 次失败 → 会话按断链处理（复用 G-6 口径）。

## §2 测试（无硬件也能跑的部分）

| 用例 | 手段 |
|---|---|
| JNI 桥参数形状 | 纯 C++ 侧 mock（注入函数指针），不依赖 JVM |
| 帧拼装/半包/粘包 | 复用 `[civ]`/`[kiss]` 既有帧解析用例，喂字节即可 |
| 拔线/无权限状态机 | 状态机单测：ready→err→断链→重连 |
| 真机 | §4，必须实物 |

## §3 许可与依赖

- CDC-ACM 走 Android 自带 `UsbDeviceConnection.bulkTransfer`，零第三方库；
- CH340/CP210x/FTDI 仅需识别 VID/PID + 波特率设置（CH340 需私有控制请求），
  参考实现（GPL/BSD 都有）只读不抄；如拷贝必须按 §0 保留许可头并登记 NOTICE。

## §4 真机验收（交付门，缺一不合并到 main 的「可用」口径）

1. 插 IC-705 CAT 线 → 台账 USB 行=可用；主屏频率/Mode 随电台变；
2. 发 `0x03` 读频往返 ≥ 1000 帧无丢帧；拔线 2 s 内台账转「缺失」；
3. KISS：经 Dire Wolf/TNC 收一条 AX.25 帧进 APRS 面板；发一条 beacon 被对端收到；
4. 权限拒绝分支：首次弹窗点「拒绝」→ 台账降级并给出「去设置授权」提示。

## §5 排期

实物到位后 2–3 天；实物未到前只合 §2 的离线用例与骨架（不亮「可用」）。