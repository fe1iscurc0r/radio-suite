# 2026-11-15 真机第三轮 · N1 / N2 / N3

对应清单：[docs/android-真机验收清单.md](../../docs/android-真机验收清单.md) §2b.2。
设备：Redmi `M2012K11AC`（MIUI，Android API 33，arm64-v8a），`org.radiosuite.app` uid 10332。
网络：PC 移动热点 `192.168.137.1` ↔ 手机 `192.168.137.177`。

| 条目 | 结论 | 证据 |
| --- | --- | --- |
| N1 组播收包实证 | **判据②通，但整条通路不算验收** —— 收包约 70 秒后彻底停止，单播也收不到，未定因 | `n1-multicast.json` |
| N2 CI-V over TCP 端到端 | **未验，卡在 PC 入站防火墙**（需用户拍板，见下） | 无 |
| N3 撤销麦克风后冷启动 | **通**（修好之后；修之前同一走法完全静默） | `n3-mic-denied.json` + 三张截图 + `n3-audio-denial.log` |

## N3 的取证走法（可重跑）

```bash
ADB=/d/Android/sdk/platform-tools/adb.exe
export MSYS_NO_PATHCONV=1

"$ADB" shell appops set org.radiosuite.app RECORD_AUDIO default
"$ADB" shell am force-stop org.radiosuite.app
"$ADB" shell pm revoke org.radiosuite.app android.permission.RECORD_AUDIO
"$ADB" logcat -c
"$ADB" shell am start -n org.radiosuite.app/org.qtproject.qt.android.bindings.QtActivity
sleep 4
# MIUI 三选弹窗的坐标每次可能不同，取真实 bounds 再点，别照截图估
"$ADB" shell uiautomator dump /sdcard/ui.xml
"$ADB" shell cat /sdcard/ui.xml   # 找 text="拒绝" 的 bounds，取中心
"$ADB" shell input tap <cx> <cy>
# 进面板页 → FT8 本机 → 音源=本机麦克风 → 开始解码
```

三个坑，都实测过：

1. **`pm revoke` 必须写全限定名** `android.permission.RECORD_AUDIO`。简写 `RECORD_AUDIO`
   报 `IllegalArgumentException: Unknown permission`。
2. **运行中撤销会被系统直接杀进程**，所以必须冷启动。这不是本应用崩溃。
3. **单设 `appops ... RECORD_AUDIO deny` 不管用**：进程不死，但采集流照跑，面板仍报
   「已解 N 槽」，logcat 里没有任何拒绝链。要走 `pm revoke` + 系统弹窗点「拒绝」，
   让 MIUI 自己把 app-op 置成 `ignore`，才会出现 `Request denied by app op`。

验完还原并回台账确认翻回 `可用`：

```bash
"$ADB" shell pm grant org.radiosuite.app android.permission.RECORD_AUDIO
"$ADB" shell appops set org.radiosuite.app RECORD_AUDIO default
```

## N1 的发送器（可重跑）

线格式照 [`apps/desktop/wsjtx/wsjtx.cpp`](../../apps/desktop/wsjtx/wsjtx.cpp) 的
`writeHeader` + `writeBody<Decode>`：全大端，`magic=0xadbccbda`、`schema=3`、`type=1`，
`QString = u32 字节数 + UTF-16BE`（**不是**码元数，也不是 UTF-8）。

本机没有可用的 Windows 版 Python（只有 WindowsApps 占位符），WSL 的 python3 在 NAT 后面
够不着热点网段，所以用 Node：

```js
// send.js —— node send.js 192.168.137.1 6
const dgram = require("dgram");
const qstr = (s) => {
  const b = Buffer.from(s, "utf16le").swap16();
  const len = Buffer.alloc(4);
  len.writeUInt32BE(b.length);
  return Buffer.concat([len, b]);
};
const decodeMsg = (ident, timeMs, snr, dt, df, mode, message) => {
  const head = Buffer.alloc(12);
  head.writeUInt32BE(0xadbccbda, 0);
  head.writeUInt32BE(3, 4);
  head.writeUInt32BE(1, 8); // MessageType::Decode
  const fixed = Buffer.alloc(21); // is_new 1 + time 4 + snr 4 + dt 8 + df 4
  fixed.writeUInt8(1, 0); // is_new
  fixed.writeUInt32BE(timeMs, 1);
  fixed.writeInt32BE(snr, 5);
  fixed.writeDoubleBE(dt, 9);
  fixed.writeInt32BE(df, 17);
  return Buffer.concat([head, qstr(ident), fixed, qstr(mode), qstr(message), Buffer.from([0, 0])]);
};
const [localIp, n] = [process.argv[2], Number(process.argv[3] || 6)];
const sock = dgram.createSocket({ type: "udp4", reuseAddr: true });
sock.bind(0, localIp, () => {
  sock.setMulticastInterface(localIp);
  sock.setMulticastTTL(4);
  const t = new Date();
  const ms = (t.getUTCHours() * 3600 + t.getUTCMinutes() * 60 + t.getUTCSeconds()) * 1000;
  let i = 0;
  const tick = () => {
    const pkt = decodeMsg("RS119-PROBE", ms, -12 + i, 0.2, 1000 + 100 * i, "FT8", "CQ BG5G XO PM01");
    sock.send(pkt, 2237, "224.0.0.1", () => {
      console.log(`sent ${pkt.length} bytes (df=${1000 + 100 * i})`);
      if (++i < n) setTimeout(tick, 1000);
      else sock.close();
    });
  };
  tick();
});
```

⚠️ 重跑时**要连着发够 2 分钟**，否则只会看到「前几十秒能收」，看不到 N1 那条未定因的停收。

## N2 为什么还没验

PC→手机方向通（N1 的包就是 PC 发出去的），**手机→PC 的 TCP 连不上**：Windows 防火墙
默认拦入站。两条路，都要用户拍板，本轮没动任何防火墙设置：

- **A：开一条限定入站规则** —— 只放 TCP 4533 / UDP 2237，来源限 `192.168.137.0/24`。
  验的是真 Wi-Fi 路径，判据①②③都能照原样走。
- **B：`adb reverse tcp:4533 tcp:4533`** —— 不动防火墙，但走的是 USB/adb 隧道，
  **绕开了 Wi-Fi 这一段**。选它就得在结论里写明「只证字节管道与链路状态，未证 Wi-Fi 路径」。

## 本轮撤回的一个误判

同一轮里曾把「解码表看着不动了」定成 `WsjtxPanel` 三张表 `insertRow(0)` 后不回顶。
**这个判断是错的，已撤回**：

- Qt 6.8.3 离屏探针实测：40 行 / 视口 4 行时 `insertRow(0)` 之后
  `verticalScrollBar().value()` 仍是 0、`maximum()` 是 36 —— 根本没有「保持旧内容可见」这回事。
- 8 张真机截图逐张核对，**每一张都是最新行在顶**。
- 加了 `processEvents()` 的变异版本照样通过，说明那条回归测试是假测试。

所以 `wsjtx_panel.cpp` 与 `test_wsjtx_ui.cpp` 的改动**全部回退**，只留下有实证的 FT8 修复。
「表看着不动」的真实原因是 N1 那条**收包停了**，见 `n1-multicast.json` 的 `open_defect`。