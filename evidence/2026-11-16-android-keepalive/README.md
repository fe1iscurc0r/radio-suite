# N4 · 监听期保活真机取证（2026-11-16）

对应待办 **#121**（`fix/listen-keep-alive`），验收条目
[`docs/android-真机验收清单.md` §2b.2 N4](../../docs/android-真机验收清单.md)。

**结论是 `partial`，不是通过。** 三件事分开说：

| | 状态 | 凭的哪个读数 |
| --- | --- | --- |
| WifiLock(HIGH_PERF) 拿到并持有 | ✅ 确证 | `dumpsys wifi` 里 `WifiLock{radio-suite-listen type=4 …}` + `WifiService: acquireWifiLock lockMode=4` |
| PARTIAL_WAKE_LOCK 拿到 | ❌ 这台 ROM 拒 | `dumpsys power` 命中数 0 + 台账里的 Java 原话 |
| 灭屏后包能不能到内核 | ⬜ **没量** | 手机没连任何 AP，PC 也不在 N1 那晚的 `192.168.137.0/24` 上 |

第三行才是 #121 的验收目标。前两行只是「机制装上了、并且如实报告自己缺一块」。

## 这台 ROM 拒绝 level 0，是查出来的不是猜的

`adb pull /system/framework/framework.jar` → `classes3.dex` → `dexdump` 定位
`PowerManager.validateWakeLockParameters`（`1fe9ec`），按 ART `sparse-switch-payload`
布局直接读字节：`ident_format=0x0200`、`size=7`、
**keys = `[1, 6, 10, 26, 32, 64, 128]`**、targets 全是 `11`。AOSP 的第一项是 `0`
（`PARTIAL_WAKE_LOCK`），这台 MIUI V140 / API 33 上不是。

同一份 dump 里 `newWakeLock:(ILjava/lang/String;)` 存在，logcat 里也能看到 GMS 持有
`PARTIAL_WAKE_LOCK 'NlpWakeLock'`。**「别的 app 拿得到」和「这台机的 2 参 public 入口拒 0」
同时为真，我们没有解释掉后者**，只记下测到的形状。

没有拿 `level=1` 去凑：`1` 不是公开常量，凑上只在这台机成立，那是把 ROM 私有行为写进产品代码。

## 三条能复用的坑

**① JNI 签名的返回类型要照设备 dex 抄，不能照类名印象写。**
`createWifiLock` 返回 `Landroid/net/wifi/WifiManager$WifiLock;`（嵌套类，
没有顶层 `android/net/wifi/WifiLock`）；`newWakeLock` 返回
`Landroid/os/PowerManager$WakeLock;`（`android/os/`，**不是** `android/os/power/`，
那是另一个同名不同门的类）。而且 Wi-Fi 已进 mainline APEX，
`createWifiLock` 根本不在 `framework.jar` 里，要拉
`/apex/com.android.wifi/javalib/framework-wifi.jar` 才 dump 得到。
这两处都写错过，**桌面 263 项测试 + arm64 交叉编译 + `aapt2` 权限核对全绿，一个都抓不到**。

**② Qt 会替你把 Java 异常吃掉，于是诊断会说假话。**
`QJniObject::callObjectMethod` 内部打印并清掉挂起异常，回头 `ExceptionCheck` 恒为空，
台账只能写「返回空对象」——把真原因（ROM 拒 level 0）说成另一个原因。
更糟的是 `acquire()` 抛异常会被当成成功、把 `*_held_` 置真，**报「已持有」而实际没持有**。
现在 create/acquire 全走 `JNIEnv` 直调，异常自己接、`toString` 进台账。

**③ 运行时文案里不许写「只在这台机上量到过」的结论，更不许把「机制已就位」写成「结果已验证」。**
初版 OK 分支是 `WifiLock(HIGH_PERF) + PARTIAL_WAKE_LOCK 都在，灭屏仍收`——两把锁在手
**推不出**灭屏收得到包，那一半恰恰是本条还没测的东西；拿它当绿灯，UI 就会替一个未做的实验签字。
缺失分支写的是 `这台机实测一灭屏就在 IP 层不收货`，把 M2012K11AC 的测量结论硬编进
**所有**安卓设备都会跑的字符串，在能拿到 wake lock 的机器上直接变成谎。
现在：OK 只报锁的状态并明写「不等于已经验到灭屏真收到包」，缺失只讲通用机制
（CPU 会 suspend、射频进省电）再拼上设备自己报的原因。
护栏是两条反向断言（`CHECK_FALSE` 含「仍收」/含「这台机」），
变异对照过：把旧文案改回去 ⇒ `#260` FAIL（`test_device_report.cpp:241`）。

## 操作上的两个自坑（不是产品缺陷）

- 「开始监听」点下去 2 秒后自停、锁被释放：真实原因是设置保存时面板**已经自动重启监听**，
  我随后那一下点击把 toggle 按钮按成了停止。少点一次就对了。
- 组播地址留默认 `224.0.0.1` 在这台机上 `joinMulticastGroup` 失败（多网卡，代码注释里就写了）。
  改成 `127.0.0.1` 即可——`bind` 一直是 `0.0.0.0`，非组播值只是跳过 join，
  外部单播照样进得来，`/proc/net/udp` 里 `00000000:08BD` 就是它。
- 想用 `cmd power acquire-wake-lock` 做旁证：这台 MIUI 回 `Unknown command`，路线不成立。

## 包身份

本轮四次装包，每次 `pm path` → `adb pull` → `sha256sum` 与构建产物比对，四次 `BYTE_MATCH=YES`：

```
26797284…  初版（JNI 签名错，装机后 logcat 抓到 NoSuchMethodError）
f3265c3a…  签名修对（但异常被 Qt 吞，台账原因失真）
ab88a233…  自己接异常，台账给出 Java 原话（但文案把设备专属结论硬编进了通用字符串）
9588b659…  本版：文案不绑设备 + 反向护栏  ← 本目录读数出自这一版
```

⚠️ `apksigner verify --print-certs` 回显的 `fb5b773a…` 是**证书摘要**，每次签名都一样，
不是 APK 身份（详见 [`../2026-11-15-android-n1-n3/README.md`](../2026-11-15-android-n1-n3/README.md) 的勘误）。

## 下一步（要人配合的那半）

手机和 PC 回到同一网段（N1 那晚是 PC 移动热点），跑
[`../2026-11-15-android-n1-n3/probe/`](../2026-11-15-android-n1-n3/probe/) 的休眠段，
判 **WifiLock 单独够不够**：

- 够 ⇒ 这台 ROM 拒掉的 WakeLock 只是遗憾，#121 可以按现状收口。
- 不够 ⇒ public `newWakeLock` 在这台机走不通，得另找路子（前台服务 / 换机验证），
  并且把它记成 ROM 级限制，而不是假装产品缺陷已修。
