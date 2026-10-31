# 安卓真机验收清单 / Android Phone Acceptance Checklist

**日期 / Date:** 2026-10-31
**适用产物:** `build-android/apps/desktop/app/android-build/build/outputs/apk/release/android-build-release-unsigned.apk`（arm64-v8a，minSdk 26 / targetSdk 34）
**代码基线:** 本清单里的 FT8 条目（B4–B6 / F1–F4）依赖 `feat/ft8-mic-source`（PR #17，栈在
PR #16 之上）——`main`（`5a08fa4`）上还没有 FT8 面板（`apps/desktop/app/` 无 `ft8_panel.*`）
也没有 `tools/ft8play`，用 main 出的包跑这些条目会找不到入口。上面那个 APK 是
2026-10-30 21:27 用本分支代码打的。
**本清单的状态:** ⚠️ **下面每一条都还没在任何真机上跑过**——本机 `adb devices` 列表为空，手上没有可连的手机。条目里写「代码事实」「现状」的部分，只记录我在 PC 上**静态核对**到的东西（源码行号、aapt 出包信息、jar 字节码），不代表真机行为。真机跑通之前，本清单不构成任何验收结论。

---

## 0. 三条阻塞：先读这段再动手

### K1 · APK 目前只能跑 mock，接不上真电台（阻塞第 7 节全部条目）

传输层只在 `main.cpp` 里按命令行开关构造，兜底一律是 `MockTransport`
（[apps/desktop/app/main.cpp:96](../apps/desktop/app/main.cpp#L96)、
[apps/desktop/app/main.cpp:118](../apps/desktop/app/main.cpp#L118)）：

```cpp
if (cli.isSet(radio_opt)) { ... Rsba1UdpTransport ... }   // 只有 --radio 才走真机
if (!transport) transport = std::make_shared<radio::core::MockTransport>();
```

而「连接设置」对话框只是把命令行**渲染成一行文本**给人看
（`apps/desktop/app/connect_dialog.cpp` 的 `cmdline_` QLabel），它自己不构造任何传输。
Android 上没有命令行输入口 ⇒ 用户装完 APK 点遍全应用也到不了真机链路。
这与既定方向「APK 直连电台优先」正面冲突，**必须先修**（三选一，见 §7 前置）。

### K2 · 但 adb 有一条能塞参数的后门（`applicationArguments`）

从 `Qt6Android.jar` 的 `QtActivityLoader.extractContextMetaData()` 字节码确认：

```java
String s = intent.getStringExtra("applicationArguments");   // ← 不过 debuggable 闸
if (s != null) appendApplicationParameters(s);
// 之后才判断 (applicationInfo.flags & 2) != 0（FLAG_DEBUGGABLE）
// 非 debuggable 时 "extraappparams" / "extraenvvars" 会被拒：
//   "Not in debug mode! It is not allowed to use extra arguments in non-debug mode."
```

所以**未签名的 release 包也能**用 `--es applicationArguments` 注入参数；`extraappparams`
那条路要 `android:debuggable="true"` 才通。当前 APK 的 manifest 里**没有** `android:debuggable`
属性（`aapt dump xmltree` 核对），即默认 false。

还有一条**打包期**的路：CMake target 属性 `QT_ANDROID_APPLICATION_ARGUMENTS`
（Qt6 `QtPlatformAndroid.cmake` 里定义，写进 manifest 的 `android.app.arguments`）。
当前出包该字段是 `android:value=""`（`build-android/.../android-build/AndroidManifest.xml:44`），
即从没设过。参数串在 native 侧按空白切词（Java 侧只把整串交给
`appendApplicationParameters`，切词实现在 `.so` 里，真机顺带确认值里带空格会怎样）。
A7 那条只能算「值得试」，试不通就退到打包注入（A8）。

### K3 · 状态栏那句 "transport: mock" 不可作为判据

空闲文本硬编码在 [apps/desktop/app/MainWindow.cpp:641](../apps/desktop/app/MainWindow.cpp#L641)，
且所有一次性提示 3 s 后都被 [apps/desktop/app/MainWindow.cpp:177-180](../apps/desktop/app/MainWindow.cpp#L177-L180)
重置回同一句 `transport: mock · radio: IC-705 sim`——**即使真的连上 UDP，它也这么写**。
本清单凡是要判「链路是什么」的条目，一律用行为（频率是否随电台变、50003 音频是否进来）
而不是这行字。

---

## 1. Gate A — 设备、签名、装上、起来

> 本机绝对路径（Git Bash 里直接可用）：
> `ADB="D:/Android/sdk/platform-tools/adb.exe"`（1.0.41 / 37.0.1-15733141）
> `BT="D:/Android/sdk/build-tools/34.0.0"`

- [ ] **A1 设备已授权**
  命令：`"$ADB" devices -l`
  判定：目标行状态为 `device`。出现 `unauthorized` = 手机上没点「允许 USB 调试」；
  `offline` = 重插或 `"$ADB" kill-server && "$ADB" start-server`。
  **现状：列表为空（本机无设备）。**

- [ ] **A2 ABI 匹配（只有 arm64-v8a）**
  命令：`"$ADB" shell getprop ro.product.cpu.abi`
  判定：输出 `arm64-v8a`。APK 的 `native-code` 只有 `'arm64-v8a'`（aapt 核对），
  x86_64 模拟器要靠 Android 11+ 的 arm 转译，不属于本清单认可的验收路径。
  装错 ABI 的失败特征是启动即 `dlopen failed`，别当成程序 bug。

- [ ] **A3 系统版本 ≥ 26**
  命令：`"$ADB" shell getprop ro.build.version.sdk`
  判定：≥ 26（manifest `sdkVersion:'26'`）。低于 26 装不上，报 `-5`。

- [ ] **A4 签名（必须先做，产物是 unsigned）**
  命令（一次性建 keystore，再对齐→签名，顺序不能反：签名后再 zipalign 会破签）：
  ```bash
  KEYTOOL="C:/Program Files/Eclipse Adoptium/jdk-21.0.12.101-hotspot/bin/keytool.exe"
  [ -f "$HOME/.android/debug.keystore" ] || "$KEYTOOL" -genkeypair -v -storetype JKS \
    -keystore "$HOME/.android/debug.keystore" -alias androiddebugkey \
    -storepass android -keypass android -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Android Debug,O=Android,C=US"
  "$BT/zipalign.exe" -f 4 <unsigned.apk> aligned.apk
  "$BT/apksigner.bat" sign --ks "$HOME/.android/debug.keystore" \
    --ks-pass pass:android --key-pass pass:android --out radio-suite-signed.apk aligned.apk
  "$BT/apksigner.bat" verify --print-certs radio-suite-signed.apk
  ```
  判定：`verify` 打出证书指纹不报错。直接装未签名包会 `INSTALL_PARSE_FAILED_NO_CERTIFICATES`。

- [ ] **A5 安装**
  命令：`"$ADB" install -r -g radio-suite-signed.apk`（`-g` 顺手给运行时权限，A6 还要单独验弹窗）
  判定：`Success`。

- [ ] **A6 冷启动不崩**
  命令：
  ```bash
  "$ADB" logcat -c
  "$ADB" shell am start -n org.radiosuite.app/org.qtproject.qt.android.bindings.QtActivity
  sleep 8; "$ADB" shell pidof org.radiosuite.app
  "$ADB" logcat -b crash -d | tail -40
  ```
  判定：`pidof` 有 pid；crash buffer 无 `F DEBUG` / 无 tombstone 引用。
  若 logcat 出现 `QtLoader: Null Intent...` 之类只在无参数启动时才会打的行，记下来但不算失败。

- [ ] **A7 参数注入通道是否真通（K2 的实测）**
  命令：
  ```bash
  "$ADB" shell am start -n org.radiosuite.app/org.qtproject.qt.android.bindings.QtActivity \
    --es applicationArguments "--radio 192.168.0.31 --user linnan --pass <现场填>"
  ```
  判定（**不看状态栏**，见 K3）：
  - 通：S 表/频率随电台实际变化出现非零变化，或 FT8 面板音源切到「电台链路」后
    能出解码行（50003 下行只有真机链路才有）；
  - 不通：始终与 mock 行为一致（mock 会把频率自己漂、PTT 自己走完）。
  记不下结论就在清单备注里写「applicationArguments 未生效」，然后走 §7 前置的代码修，
  别在 adb 参数上继续耗。

- [ ] **A8 A7 不通时的退路：打包期钉参数**
  在 `apps/desktop/app/CMakeLists.txt` 的 target 上临时加：
  ```cmake
  set_property(TARGET radio-suite PROPERTY QT_ANDROID_APPLICATION_ARGUMENTS
                                           "--radio 192.168.0.31 --user linnan")
  ```
  再 `cmd //c "build\android_build.bat apk"` 重打，回 A4 起走一遍。
  判定：`grep android.app.arguments build-android/apps/desktop/app/android-build/AndroidManifest.xml`
  的值不再是 `""`，且行为判据同 A7。
  **口令不要写进这行、更不要提交**——现场填或走环境变量，验完 revert 掉这个属性。

---

## 2. Gate B — 麦克风权限与 AAudio

桌面侧这条链路的旧结论已经翻了：早先记的「WASAPI 共享模式只拿到 57%/73% 样本」是
**测量口径错**（把 `start()` 到首个回调之间的墙钟算成了丢样）。带时间戳探针重测：
首个采集回调 **0.44–0.51 s** 才到（共享模式 `InitializeSharedAudioStream` 的启动开销），
首帧之后 20 ms 恒定、预热 1 s 再测 2 s 的稳态样本预算 **100.1%**。定案见 PR #15
（`fix/wasapi-capture-measurement`，**尚未合并**——本分支的
`docs/notes/audio-capture-wasp-diagnosis.md` 还是旧版）。Android 的 AAudio 是另一套实现，
这一节按同一个口径重测：先预热，再看稳态，别拿桌面数字当预期。

- [ ] **B1 RECORD_AUDIO 运行时弹窗**
  代码事实：只在主窗构造时、权限状态为 `Undetermined` 才申请一次
  （[apps/desktop/app/MainWindow.cpp:96-105](../apps/desktop/app/MainWindow.cpp#L96-L105) 的 `Q_OS_ANDROID` 块）。
  判定：全新安装首次启动**弹出**系统授权框；点「不允许」后应用照常起，且 FT8 面板选
  「本机麦克风」时如实失败/不产出解码行，**不崩、不静默假装在采集**。
  复测命令（先撤销再看行为）：
  ```bash
  "$ADB" shell pm revoke org.radiosuite.app android.permission.RECORD_AUDIO
  "$ADB" shell dumpsys package org.radiosuite.app | grep -A6 "runtime permissions"
  ```

- [ ] **B2 提前授权路径**
  命令：`"$ADB" shell pm grant org.radiosuite.app android.permission.RECORD_AUDIO`
  判定：grant 后直接启动不再弹框（用于把 B1 之外的条目和权限弹窗解耦）。

- [ ] **B3 AAudio 输出真实出声**
  操作：FT8 面板点「定时发射」（整槽 15 s），或先用 PC 侧同一套渲染逻辑做对照。
  命令：
  ```bash
  "$ADB" shell dumpsys media.audio_flinger | grep -iE "AAudio|underrun|power state" | head -20
  ```
  判定：听得到 15 s 的 FT8 音（约 1500 Hz 一组 tones）；`pidof` 不变；
  underrun 计数不持续上涨。若完全静音，先分清是没权限/设备号错，还是 `AudioOut` 在
  Android 只回 `kDefaultDeviceId` 这条已知简化的后果。

- [ ] **B4 AAudio 采集真拿到样本**
  操作：FT8 面板音源选「本机麦克风」→ 开始解码。
  判定：面板状态行从「等待…」进到「解码中…」，并且**已解槽数每 15 s 递增**。
  代码依据：`SlotDecoder::fedSamples()` 区分「对齐等待」与「真没音频」，所以这条一旦显示
  「已解 N 槽」就是真收到样本，不是界面话术。

- [ ] **B5 首帧延迟与稳态预算**
  操作：起 FT8 麦克风解码后**先让它跑 1 s 预热**，再连续跑 ≥ 4 个时隙（60 s）观察。
  判定：「已解」计数每 15 s 稳定 +1、不跳槽；首个解码结果在 start 后 ≤ 2 s 内出现属正常
  （对应桌面那段启动开销）。跳槽 = 真欠载，记进 §9 并按机型注明，不要顺手调阈值。

- [ ] **B6 半双工抢占如实拒绝**
  操作：同时打开网络音频（50003）和 FT8 麦克风音源。
  判定：后开的一方**明确拒绝**并给出原因，而不是两个都"在跑"却只有一个出声
  （桌面侧这条已有实现与用例，Android 只是重验设备枚举差异）。

---

## 3. Gate C — 手机分辨率上的 UI 与触控

- [ ] **C1 窗口尺寸**
  代码事实：[apps/desktop/app/main.cpp:121](../apps/desktop/app/main.cpp#L121) 无条件 `win.resize(1280, 720)`。
  命令：`"$ADB" shell wm size` + `"$ADB" shell wm density`
  判定：截图（`"$ADB" exec-out screencap -p > shot.png`）里频谱全宽、无横向滚动条、
  右侧按钮列不越界、频率大字不被压。桌面验收口径「1105×768 不越界」在手机竖屏上**不成立**，
  这条预期是**要改的**，改法先记问题不先动代码。

- [ ] **C2 触摸目标 ≥ 48dp**
  命令：`"$ADB" shell wm density` 取 `densityDPI`，按 `dp = px * 160 / densityDPI` 换算。
  判定：旋钮、CW Set Mode 树、Memory 列表、频段按钮最小可点边长 ≥ 48dp；
  连点 5 次同一目标，命中 5 次（错点一次即记不通过）。

- [ ] **C3 旋转不重启进程**
  代码事实：manifest `android:configChanges` 已含 `orientation|screenSize|density|…`
  （值 `0x40003ff7`），`android:screenOrientation="unspecified"`。
  命令：`pid_before=$("$ADB" shell pidof org.radiosuite.app)` → 转屏 → 再取。
  判定：pid 不变，且频谱/解码表内容延续。pid 变了 = configChanges 没兜住，属真缺陷。

- [ ] **C4 软键盘输入**
  操作：点频率框 / FT8 文本框，让软键盘起来。
  判定：输入框不被键盘遮住（遮了就是必须滚动的布局缺陷）；能输入、能回删；
  按「完成/回车」不触发误发射。

- [ ] **C5 独立面板窗口**
  代码事实：APRS / WSJT-X / HamLog 工作台 / FT8 面板都是独立顶层窗口（父为空）。
  Android 上一个进程只有一个 Activity，多顶层窗口通常退化成一个全屏 SurfaceView。
  判定：四个面板都能打开、能操作、返回键只关面板不关主窗；
  **打不开或一开就盖死主窗**→ 记为安卓端结构性缺陷，不是可绕过的小毛病。

- [ ] **C6 深色底 + 小屏可读性**
  判定：截屏里瀑布、S 表读数、状态行不叠字（桌面 2026-10-29 就抓到过右半屏越界读与表针压字，
  手机缩放后同类问题更易复现）。

---

## 4. Gate D — 持久化（Android 上是重灾区）

- [ ] **D1 QSettings 到底存不存**
  代码事实：全应用用**默认构造** `QSettings st;`，org/app 都设成了 `radio-suite`
  （[apps/desktop/app/main.cpp:23-25](../apps/desktop/app/main.cpp#L23-L25)），共 10 处：
  [MainWindow.cpp:119/440/467/475/1161/1173/1213](../apps/desktop/app/MainWindow.cpp#L119)、
  [wsjtx_panel.cpp:233/441/479](../apps/desktop/app/wsjtx_panel.cpp#L233)。
  默认格式 = `NativeFormat`，Android 没有原生注册表/plist 后端。
  操作：设音频设备 + 把 FT8 音源切成麦克风 → `"$ADB" shell am force-stop org.radiosuite.app`
  → 重启 → 回看两处设置。
  判定：**重启后仍在** = 通过；回到默认值 = 缺陷（修法方向：显式
  `QSettings(QSettings::IniFormat, QSettings::UserScope, ...)` 或落在 `QStandardPaths::AppDataLocation`）。
  辅助命令（只有 debuggable 包能用 `run-as`）：
  ```bash
  "$ADB" exec-out run-as org.radiosuite.app sh -c 'ls -l files/ 2>/dev/null; find . -name "*.conf" -o -name "*.ini" 2>/dev/null'
  "$ADB" logcat -d | grep -i "qsettings"
  ```

- [ ] **D2 HamLog 在 Android 上必然「不可用」，要的是如实降级**
  代码事实：[apps/desktop/logbook/logbook.cpp:21-25](../apps/desktop/logbook/logbook.cpp#L21-L25) 读 `APPDATA`，
  为空直接 `return {}`；Android 没有 `APPDATA` ⇒ 路径恒空。
  判定：HamLog 相关按钮**不崩**、给出「库不存在（先运行一次 HamLog）」一类的真实提示、
  且不出现「写了 QSO 但其实没落盘」的假成功。
  这条判的是「诚实」，不是「能用」；要能用得换路径策略，属后续工单。

- [ ] **D3 APRS 库落在哪**
  代码事实：路径解析顺序 `RADIO_SUITE_APRS_DB` → `APPDATA` → `QStandardPaths::AppDataLocation`
  （[apps/desktop/aprs/persist.cpp](../apps/desktop/aprs/persist.cpp)）。
  判定：收下若干 APRS 站点 → force-stop → 重启，历史轨迹仍在，且能在 `run-as` 列出的
  私有目录里看到那个 `.db` 文件（大小非 0）。

- [ ] **D4 卸载即清（不留脏数据）**
  命令：`"$ADB" uninstall org.radiosuite.app` 后重装。
  判定：全部设置回到默认（说明数据确实只在应用私有区，没写到外部公共目录）。

---

## 5. Gate E — 生命周期、网络与音频焦点

- [ ] **E1 灭屏 30 s 再亮屏**
  命令：`"$ADB" shell input keyevent 26`（电源键灭屏）→ 等 30 s →
  `"$ADB" shell input keyevent 26` 唤醒 + `"$ADB" shell input keyevent 82` 解锁。
  判定：进程存活（`pidof`）；回前台频谱继续动；若采集/播放停了，重启音频后能恢复。
  机型注意：设了 PIN/图案锁屏时 `keyevent 82` 解不开，需要人工解锁，别把「解不开锁」记成缺陷。

- [ ] **E2 切后台再回来**
  命令：`"$ADB" shell input keyevent 3`（Home）→ 等 20 s → 再 `am start` 拉回。
  判定：不白屏、不 ANR（`"$ADB" logcat -d | grep -i "ANR in org.radiosuite.app"` 为空）、
  回来继续收发。

- [ ] **E3 音频焦点被抢**
  操作：应用正在出声时，让手机放系统提示音/来电。
  判定：焦点归还后继续出声，不永久静音。

- [ ] **E4 Wi-Fi 可达性（真机链路的前置）**
  命令：`"$ADB" shell ip addr show wlan0` + `"$ADB" shell getprop dhcp.eth0` 或
  `"$ADB" shell settings get global wifi_on`
  判定：手机与电台同网段（例：电台 `192.168.0.31` → 手机应落在 `192.168.0.x`）。
  **注意**：手机在移动数据/隔离网段时 UDP 50001/50003 永远不通，这会让 §7 全红但代码没错。

- [ ] **E5 低内存回收后重建**
  操作：开几个大应用逼系统杀 radio-suite，再从最近任务恢复。
  判定：重建后不崩、不留下「已发射但 PTT 卡住」的状态（发射出口与门禁在
  `~MainWindow` 里是先停音频再析构成员，见 [MainWindow.cpp:188-199](../apps/desktop/app/MainWindow.cpp#L188-L199)）。

---

## 6. Gate F — FT8 时间基准（最容易静默翻车）

- [ ] **F1 手机 UTC 偏差**
  命令：`"$ADB" shell date -u +%s`，与本机 `date -u +%s` 相减。
  判定：`|Δ| ≤ 1 s`。桌面实测发射波形尾窗只有 **1.83 s**（79 符号 = 13.17 s 装在 15 s 槽里，
  且 `n_lead = 0` 顶格槽起点，见 [apps/desktop/ft8/ft8_codec.cpp](../apps/desktop/ft8/ft8_codec.cpp)），
  1 s 偏差已经吃掉一半余量。Android 拿不到毫秒：toybox `date` 不支持 `%N`，
  要更严就开「使用网络时间」后用 NTP 侧对照，别在设备上假装有毫秒。

- [ ] **F2 手机收：PC 播 → 手机解**
  命令（PC 侧，**在仓库根目录**跑；`ft8play.exe` 需先 `build\msvc_build.bat ft8play`）：
  ```bash
  now=$(date +%s%3N); sleep $(awk -v m=$(( 15000 - now % 15000 )) 'BEGIN{printf "%.3f", m/1000}')
  PATH="/d/Qt/6.8.3/msvc2022_64/bin:$PATH" build/tools/ft8play.exe "CQ BG5GXO PM01" 1500 0.6
  ```
  手机侧：FT8 面板音源=本机麦克风 → 开始解码。
  判定：解码表出现 `CQ BG5GXO PM01` 行（DT 应为个位数秒内、SNR 可低）。
  已知不判失败的项：**声学 DF 会跳到 250 Hz 的整倍数**（房间响应 + 采集侧 AGC/削波造成，
  离线渲染→解码在 500/1000/1500/2000 Hz 四处 DF 全部准确，所以这是通道伪影不是代码缺陷；
  见 `docs/HANDOVER.md` §7 已知坑第 14 条），DF 数值不作为通过条件。

- [ ] **F3 手机发：整槽出声 + 自动回 RX**
  操作：手机 FT8 面板填文本 → 频偏 1500 → 定时发射。
  判定：到点**准点**起播（不早不晚，桌面修过一次「居中导致每发晚 0.92 s」）；
  按钮标签在发射触发那一刻落回「定时发射」
  （这条有专门用例：状态与标签不许分叉，留「取消发射」会把下一刀变成再发 15 s）；
  播完自动回 RX。

- [ ] **F4 手机自环（可选，难度高）**
  手机扬声器播、同机麦克风收。判定：能解出自己的发射即通道闭环；
  解不出**不代表代码坏**（自听受机型影响太大），只记录现象。

---

## 7. Gate G — 真机链路（当前被 K1 阻塞）

**前置（三选一，先做再跑本节）：**
1. 在 `main.cpp` 之外增加应用内构造入口：把 `ConnectDialog` 从「渲染一行命令文本」
   改成真返回 host/user/pass 并让 `MainWindow` 换 transport；
2. 从 `QSettings`（或启动意图参数）读链路配置，启动时决定 transport；
3. 打包期用 `QT_ANDROID_APPLICATION_ARGUMENTS` 把 `--radio <IP>` 等钉进 manifest（= A8，
   演示包可用；口令不进仓库，长期不够）。

- [ ] **G-1** 行为判据：mock 的频率会自漂、PTT 自己走完时序；真机链路不会自漂。
- [ ] **G-2** 读回频率与电台面板一致（差 0 Hz）。
- [ ] **G-3** 写频往返：应用设 14.074.000 → 电台面板同值。
- [ ] **G-4** 50003 下行音频进频谱/瀑布，FT8 音源选「电台链路」能出解码行。
- [ ] **G-5** PTT 一键发射在真电台上出声，且应用状态回 RX。
- [ ] **G-6** 断链（拔网线/电台关机）后 UI 如实显示失联，不谎报在线。

以上六条在「前置」那三选一落地之前**一律标 BLOCKED**，不要靠 mock 行为打勾——那会写出假报告。

---

## 8. 证据采集（跑每一项之前先起好）

```bash
"$ADB" logcat -c && "$ADB" logcat -v time -s Qt:V QtLoader:V ActivityManager:W libc:F \
  | tee android-run.log          # 另开一个窗口常驻
"$ADB" exec-out screencap -p > shot-$(date +%H%M%S).png
"$ADB" shell dumpsys gfxinfo org.radiosuite.app framestats | head -60   # 卡不卡
"$ADB" logcat -b crash -d                                               # 崩了看这里
"$ADB" bugreport android-bug.zip                                        # 疑难杂症最后一招
"$ADB" shell ls -l /data/tombstones 2>/dev/null                         # 非 root 通常读不到，用 bugreport
```

崩溃时**必须**留：`pidof` 结果、crash buffer 尾部 40 行、对应截图、复现到第几步。

---

## 9. 跑完之后

- 每条把「判定标准」原样替换成**实测值**（数字、截图文件名、命令输出摘要），不要只打勾。
- 结论落到本文件顶部「本清单的状态」那行：什么机型、什么 Android 版本、多少条通过/失败。
- 失败条目按严重性分成：阻塞（K1 类结构性缺口）/ 缺陷（代码错）/ 机型限制（记录不修）。
- 改动走新分支 + PR，不直接动 `main`。
