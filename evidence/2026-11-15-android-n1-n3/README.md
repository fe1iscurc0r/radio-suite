# 2026-11-15 真机第三轮 · N1 / N2 / N3

对应清单：[docs/android-真机验收清单.md](../../docs/android-真机验收清单.md) §2b.2。
设备：Redmi `M2012K11AC`（MIUI，Android API 33，arm64-v8a），`org.radiosuite.app` uid 10332。
网络：PC 移动热点 `192.168.137.1` ↔ 手机 `192.168.137.177`。

| 条目 | 结论 | 证据 |
| --- | --- | --- |
| N1 组播收包实证 | **判据②通** —— 真机收到组播包并出了解码行；「约 70 秒后停收」已定因为是屏幕休眠（非组播缺陷），锁屏挂机保活另列待办 | `n1-multicast.json` + `n1-rootcause.json` |
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

⚠️ 重跑时**要连着发够 2 分钟**，否则只会看到「前几十秒能收」，看不到 N1 那条停收。

## N1 那条「70 秒停收」的定因（当晚补测）

结论先说：**链路上没有 70 秒这道墙**。停收的分界线是屏幕状态。

| 段 | 窗口 | 屏幕 | 结果 |
| --- | --- | --- | --- |
| A 休眠 | 17:34:14–17:35:13 | `Asleep` | ICMP 12 发 0 收，整分钟全超时 |
| B 亮屏 · ICMP | 17:36:46–17:37:50 | 每 10 秒唤醒，6 次采样全 `Awake` | ICMP 60 发 60 收，0% 丢，RTT 22–121 ms |
| B 亮屏 · UDP | 17:38:40–17:40:31 | 同上 | 单播 90 包 / 110 秒，`rx_queue` 0 → 214016 字节，打满后才开始 drops 0→4 |

读法：

- 休眠态连 ICMP 都不回。原始现象里「改单播也 0 收」正是这个特征——休眠时不是丢了组播，
  是整机在 IP 层不吭声。
- 亮屏态包一路到内核，跨过 70 秒还在涨，所以不是组播 / IGMP / socket 的事。
- 另一层在应用侧：进程退到后台就不再读 socket，`rx_queue` 只涨不消，涨满才 drops。

**没证到的**：原始那轮（16:06–16:09）没记录屏幕状态，所以这是「机制已实测且与症状逐条吻合」，
不是当场逐字复现；B 段亮屏是靠每 10 秒发一次 `input` 维持的，混杂了「有输入」这一因素；
UDP 段只证到**内核收包**，没证应用解码（当时应用在后台，正是它不读的那半边）。

重跑：`probe/ping-ab.sh`（ICMP 段）、`probe/udp-awake-probe.js`（UDP 段）。

**真正的产品缺口（还没修）**：`apps/desktop/app/multicast_lock.cpp` 只拿了
`WifiManager.MulticastLock`——那只关掉 Wi-Fi 的组播过滤，不负责让射频或 CPU 保持醒着。
监听期间缺 `WifiLock(WIFI_MODE_FULL_HIGH_PERF)` + `PARTIAL_WAKE_LOCK`，
而且 UI 不会告诉用户「已经收不到了」。手机一锁屏，挂机收包就静默失效。
详见 `n1-rootcause.json` 的 `product_gap`。

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
「表看着不动」的真实原因是 N1 那条**收包停了**——而收包为什么停，见下面「N1 定因」一节：
是屏幕休眠，不是通路缺陷。`n1-multicast.json` 的 `open_defect` 也已标成已定因。

## 勘误：`n3-mic-denied.json` 原来的 APK 哈希是错的

`build_under_test.apk_sha256` 原本写的是 `fb5b773a…`。**2026-11-16 复核发现对不上**：

| 对象 | 字节数 | sha256（前 16 位） |
| --- | --- | --- |
| 机上现装包（`lastUpdateTime=2026-11-15 17:05:21`，此后未重装） | 12,689,808 | `cef817002aba2262` |
| 当时构建目录留存的 unsigned 包 ×2 | 12,670,889 | `b6a3d46abb29380f` |
| 原记录声称的值 | — | `fb5b773acd68ceb7` ← **不是任何文件的哈希** |

也就是说：**字节数一直是对的，哈希抄错了行**，不能当身份标识。
JSON 里已改成存机上现装包的实测哈希，并保留错值与来源链（`pm path` → `adb pull` → `sha256sum`），
不偷偷覆盖。

✅ **2026-11-16 22:00 破案**（当时写的是「来源已不可考」，现在有了确证）：
`fb5b773a…` 是 **debug 签名的证书摘要**，不是 APK 的文件哈希。
`apksigner verify --print-certs aligned.apk` 回显

```
Signer #1 certificate SHA-256 digest: fb5b773acd68ceb7512617fd65d2b25124ddb19a0b606f37be2a9cf073d59873
```

与那条记录逐字节相同。它由 keystore 决定，**每次签名都一样**，所以它连「哪一次构建」都指认不了——
这也解释了为什么拿任何 APK 去 `sha256sum` 都算不出它。

影响范围要说清：**不影响 N3 的结论**。拒绝链 logcat、面板红字文案、台账「缺失→可用」
三张截图、appop/permission 状态这些都是独立读数，不依赖这个哈希；受影响的只有
「这一包能不能被唯一指认、能不能原样重装」。

⚠️ **备份不在这个仓库里**，12 MB 的二进制不该入库。它现在只躺在操作机的临时目录
`%LOCALAPPDATA%\Temp\rs-121-installed-backup.apk`（sha256 `cef81700…`），**会话级、随时可能被清**。
要重装：`adb install -r <那份备份>`；备份没了就按上面的命令从机上再拉一次，
或者从当前 main 重新构建一个签名包（内容与当时不同，别再拿 `cef81700…` 指认它）。

🔑 可复用的教训，两条：

1. **取证当天就该把签好名的 APK 原件挪到一个不会被构建覆盖的目录再记哈希**。当时签名包就躺在
   `build-android/.../outputs/` 里，被后续构建冲掉了，所以现在**无法从仓库重建出同一个字节的包**
   ——只能靠机上这份拉下来的备份。
2. **包身份只认「对这个文件本身算出来的哈希」**。`apksigner --print-certs` 的输出里同时有
   证书摘要和（另一条命令才有的）文件哈希，抄错一行就会得到一个谁也算不出、且每次签名都相同的值；
   这种值看起来像标识，实际什么都不是。现在每次装完都 `pm path` → `adb pull` → `sha256sum`
   与构建产物比对，字节相同才算装上（#121 起照这条走，三次装包全部 BYTE_MATCH=YES）。