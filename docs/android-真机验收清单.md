# 安卓真机验收清单 / Android Phone Acceptance Checklist

**日期 / Date:** 2026-10-31
**适用产物:** `build-android/apps/desktop/app/android-build/build/outputs/apk/release/android-build-release-unsigned.apk`（arm64-v8a，minSdk 26 / targetSdk 34）
**代码基线:** 本清单里的 FT8 条目（B4–B6 / F1–F4）依赖 `feat/ft8-mic-source`（PR #17，栈在
PR #16 之上）——`main`（`5a08fa4`）上还没有 FT8 面板（`apps/desktop/app/` 无 `ft8_panel.*`）
也没有 `tools/ft8play`，用 main 出的包跑这些条目会找不到入口。K1/K3 的代码修和 G-0 那条
在 `fix/android-link-prefs`（PR #17 之上再叠一层），D1 的统一设置后端再叠它一层
（`fix/android-settings-persistence`）。上面那个 APK 是
2026-10-30 21:27 用本分支代码打的，**不含**这些改动。2026-10-31 在本分支重新出过一包并装到
手机上（含 D1 的落盘路径修复与对话框宽度取小），K4 就是它的实测结果。
**本清单的状态:** 2026-10-31 手机第一次接上并跑了 K4 那批条目（装包、D1 持久化、G-0 的
「保存→重启→按偏好建链路」这半条、K3 的离线不谎报）。**除 K4 列出的之外，其余每一条仍是
PC 上静态核对的「代码事实」，没有真机结论。** 条目里写「现状」的部分只记录源码行号、aapt
出包信息、jar 字节码，不代表真机行为。

## K4 · 真机首跑记录（2026-10-31，Redmi K40 / Android 13 / arm64-v8a / MIUI V14.0.6）

装的包 = `fix/android-settings-persistence` 工作树（含 D1 的落盘路径修复 + 对话框宽度按屏
取小）交叉编译 + `androiddeployqt` 出 APK，`zipalign` 后 `apksigner` 用 debug keystore 签。

**跑通了的：**

- **A1–A6**：设备授权、arm64-v8a、minSdk 26 全对；冷启动不崩，logcat 里 tag `radio-suite`
  打「链路： mock （来源： 默认 ）」+「设置文件： …」。安装见下面第 1 条坑。
- **D1 落盘**：应用**自己**写出 `/data/user/0/org.radiosuite.app/files/radio-suite.ini`，
  `[mem] M1\freq=14270000 M1\mode=1`（点 M1「存」）与 `[ft8] source=1`（FT8 音源切麦克风）
  都真在文件里。改动前的两个落盘点都写不进去，见 D1 正文。
- **D1 读回**：`am force-stop` → 不带任何参数 `am start` → FT8 面板音源仍是「本机麦克风」，
  且状态行是「本机麦克风 · 已解 0 槽」（采集真起来了，B1 的授权也保住了）。
- **G-0 的配置半条**：对话框链路选「C++ 原生 UDP」→ 保存 → 状态栏闪「链路设置已保存（udp），
  重启 radio-suite 生效」→ ini 里 `[link] mode=1` → force-stop → 重启 → logcat 打
  `链路： udp （来源： 持久化偏好 ）`，空闲状态栏 `transport: udp · 192.168.0.31`。
  重开对话框下拉框回填 UDP（不是回到 mock）。
- **K3 不谎报**：那个 IP 这里没有电台，logcat 是
  `W transport open failed: "ctrl 信道无法连接 192.168.0.31"`，主窗显示 `0.000000 MHz` /
  `USB/0` 而不是 mock 的 14.270 MHz。**没有悄悄折回 mock**，这正是 G-0 要的判据。
- **C1/C6 竖屏（一半好一半坏）**：连接对话框在竖屏（1080 宽）下「保存」「关闭」都在屏内——
  加宽度取小之前「关闭」的 bounds 是 `[0,0][0,0]`，即整排被 460 的 `setMinimumWidth` 挤出屏外。
  但**主窗在竖屏是叠的**：右列大按键压住「音频设置」那一行，频段/模式/内存三行被裁，
  频率大字显示成「270000」。详见 C1/C2，这一条本轮只记不动代码。
- **D3（只验了降级形态）**：APRS 面板给的是真话——状态栏「APRS 保存失败：Qt SQLite 驱动
  不可用」，因为包里没打 QSQLITE 插件。不是假成功。

**没跑、也跑不了的：**

- G-1…G-5（真电台的行为判据）：**手上没有可连的电台**，只有全盛 UVK6（不是 IC-705，
  没有 50003 音频口）。所以「手机上点保存 → 重启 → 真连上电台」这条只到「按偏好建出了
  UDP 链路并且老实报连不上」为止。
- Gate B（AAudio 真出声/真采集）、Gate E（生命周期/音频焦点）、Gate F（FT8 时间基准）、
  A7/A8（参数注入通道）、D4（卸载即清）——这一轮没有覆盖。

** adb 驱动这台手机的五个坑（下次照清单跑之前先看这段）：**

1. **MIUI 14 拦 `adb install`**：报 `INSTALL_FAILED_USER_RESTRICTED`。「USB 调试（安全设置）」
   是安全设置，别自动化去翻它；这台机器有 Magisk root，直接
   `su -c 'pm install -r /data/local/tmp/xxx.apk'` 就过了。
2. **`uiautomator` 报的对话框按钮 bounds 右偏约 126px**（正好一个按钮 + 间距），照 bounds
   中心点 `input tap` 会命中「关闭」而不是「保存」。主窗按钮不偏。更阴的是**两个按钮都会
   关掉对话框**，所以「对话框关了」不能当保存成功的判据——要么看 ini 里 `[link]` 有没有落盘，
   要么看状态栏那句闪的提示。
3. **软键盘盖住按钮排**：密码框一获得焦点 IME 就弹起，横屏盖到 y≈709、竖屏盖到 y≈1291–1416，
   也就是「保存/关闭」整排；这时候 tap 与 BACK 都像没反应。`input keyevent 111`（ESCAPE）
   收起 IME（`dumpsys input_method` 里 `mInputShown=false`）后触控投递就恢复正常。
4. **密码框会让截屏失效**：`QLineEdit::Password` 置 FLAG_SECURE，此时 `screencap` 返回
   **0 字节**。要截图取证得先让密码框失焦；`uiautomator dump` 不受影响。
5. **`uiautomator dump` 只报最上层窗口**，弹层（QComboBox 的 view）另算一层。判「对话框
   还在不在」要认里面那句独有的文本，别认「有没有节点」。


---

## 0. 三条前提：先读这段再动手

### K1 · APK 只能跑 mock —— 代码侧已修（`fix/android-link-prefs`），真机已验到「按偏好建出 UDP 链路」

原缺口：传输层只在 `main.cpp` 里按命令行开关构造，兜底一律 `MockTransport`；
「连接设置」对话框只把命令行**渲染成一行文本**，自己不构造任何传输；
Android 上没有命令行输入口 ⇒ 装完 APK 点遍全应用也到不了真机链路。

现在的形状（`apps/desktop/app/launch_config.{h,cpp}`，GUI 与 headless 共用同一个工厂）：

- 优先级 **命令行 > 持久化偏好 > mock**，在
  [main.cpp:50-56](../apps/desktop/app/main.cpp#L50-L56) 一处算完；
- 偏好读写在 [launch_config.cpp:69-95](../apps/desktop/app/launch_config.cpp#L69-L95)，
  存储后端在 [ui_common/app_settings.h:14-22](../apps/desktop/ui_common/app_settings.h#L14-L22)：
  Android 上显式走 `QSettings::IniFormat`（默认构造的 NativeFormat 在安卓没有后端，
  见 D1），键为 `link/mode|host|user|pass|python`；
- 对话框的「保存」真的落到那套偏好里
  （[connect_dialog.cpp:141](../apps/desktop/app/connect_dialog.cpp#L141) →
  [MainWindow.cpp:290-300](../apps/desktop/app/MainWindow.cpp#L290-L300)），重启生效；
- 建不出来的链路（本构建缺 Qt6::Network / 电台 IP 为空 / 平台上没有 Python 桥）
  在 [launch_config.cpp:25-33](../apps/desktop/app/launch_config.cpp#L25-L33) 折回真话，
  不让状态栏与实际链路各说各的。

离屏用例覆盖到优先级、空 IP 降级、ini 往返、对话框回填/保存
（[tests/test_launch_config.cpp](../tests/test_launch_config.cpp)，9 例）；
Android 交叉编译侧只验证了**编得过、链得上**（`build-android/.../libradio-suite_arm64-v8a.so`）。
**「手机上点保存 → 重启 → 真连上电台」这一条仍然要靠 A7/A8/G 节实测**，A5 之后照 G 节走。

桌面实窗等价验证已做过一轮（2026-10-31，非手机）：保存写通、不带参数重启后
状态栏读回 `transport: udp · <IP>`、连不上时频率落 0 / S 表 -999 不谎报在线
（过程与结论见 `docs/HANDOVER.md` §5 第 11 条）。**这只证明代码逻辑通，不证明
Android 的 ini 落盘与启动路径通**——G-0 照跑不误。

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

### K3 · 状态栏那句 "transport: mock" —— 硬编码已去掉，但**仍不作为判据**

原先空闲文本硬编码在 `MainWindow.cpp`，所有一次性提示 3 s 后又被重置回同一句
`transport: mock · radio: IC-705 sim`——**即使真的连上 UDP 也这么写**。
现在两处都改成由链路偏好派生
（[MainWindow.cpp:647](../apps/desktop/app/MainWindow.cpp#L647) 建标签、
[MainWindow.cpp:177-180](../apps/desktop/app/MainWindow.cpp#L177-L180) 恢复空闲文本，
内容来自 [launch_config.cpp:144](../apps/desktop/app/launch_config.cpp#L144)），
UDP 链路下会长成 `transport: udp · <电台 IP>`。

但这条字仍然只说明**程序以为自己在跑哪条链路**（偏好折回真话之后的取值），
不证明电台真的应答。凡是要判「链路通没通」的条目，照旧用行为：
频率是否随电台变、50003 音频是否进来、PTT 相位是否被电台推着走。
**真机侧（2026-10-31）确认了它「不谎报」这一半**：偏好为 UDP 且电台不存在时，空闲文本是
`transport: udp · 192.168.0.31`、主窗 `0.000000 MHz`，不再写回 mock 的 14.270。
「真连上时也说真话」那一半仍等一台真电台（G-1 起）。

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
  判定：
  - 参数**进没进到 `QApplication::arguments`** 这一层现在可以直接看状态栏
    （K3 已修：命令行 > 偏好 > mock，走命令行时文本会长成 `transport: udp · <IP>`）；
  - 链路**通没通**照旧不看状态栏，看行为：S 表/频率随电台实际变化出现非零变化，
    或 FT8 面板音源切到「电台链路」后能出解码行（50003 下行只有真机链路才有）；
  - 不通：始终与 mock 行为一致（mock 会把频率自己漂、PTT 自己走完）。
  状态栏写了 mock 而参数里明明有 `--radio <IP>`，说明被降级判定折回了（IP 空 / 本构建缺
  Qt6::Network），先查 `linkPrefsFromCli` 那条链，别在 adb 参数上继续耗。
  这条路只是**调试通道**：日常用 G-0 的应用内设置，不依赖 adb。

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

- [ ] **C1 窗口尺寸** —— **真机不通过**（2026-10-31 竖屏实测，见下面「实测」）
  代码事实：[apps/desktop/app/main.cpp:121](../apps/desktop/app/main.cpp#L121) 无条件 `win.resize(1280, 720)`。
  命令：`"$ADB" shell wm size` + `"$ADB" shell wm density`
  判定：截图（`"$ADB" exec-out screencap -p > shot.png`）里频谱全宽、无横向滚动条、
  右侧按钮列不越界、频率大字不被压。桌面验收口径「1105×768 不越界」在手机竖屏上**不成立**，
  这条预期是**要改的**，改法先记问题不先动代码。
  **实测（`wm size`=1080x2400，`wm density`=440，无 `display_density_forced` ⇒ 2.75x，
  竖屏可用宽度只有 393dp）**：
  - **竖屏：主窗两列直接叠在一起。** 「音频设置」的 bounds 是 `[674,949][904,1042]`，
    右列「CW 键控」是 `[707,977][1064,1094]`，两者交叠；照 bounds 中心点 (789,995) 点下去
    **开的是 CW 面板**（截图核对过，不是 a11y 报错误差）。频段按钮行、模式行（LS/US/AM/CW/…）、
    内存行都被右列裁掉一半，频率大字显示成「270000」（`14.270000` 被裁）。
    也就是说：竖屏下**主窗不能靠 bounds 定位去点**，这一条比 D1 更挡手机日常使用。
  - **横屏：能开能用**（K4 那批条目全在横屏跑的），但频谱只占左下一小块，
    且布局明显在压缩自己的最小尺寸（见 C2）。
  - 连接对话框单独一条：加宽度取小之前竖屏「关闭」的 bounds 是 `[0,0][0,0]`（整排在屏外），
    加完之后竖屏最右元素落在 x=998（屏宽 1080），保存/关闭都在屏内。
  **改法留给下一轮拍板**，两条路：① manifest 上 `sensorLandscape`，承认「手机当控制台就是横屏」，
  一行改动、竖屏不再摆一个点不动的界面给人看；② 真做一套手机布局（右列收成底部标签栏、
  仪表/调谐压缩），那是独立一轮的活。①便宜但砍掉竖屏使用场景，②才对得上「手机当手持控制台」的
  初衷——**这条是产品形状的决定，不要顺手改**。

- [ ] **C2 触摸目标 ≥ 48dp** —— **真机不通过**（2026-10-31 实测）
  命令：`"$ADB" shell wm density` 取 `densityDPI`，按 `dp = px * 160 / densityDPI` 换算。
  判定：旋钮、CW Set Mode 树、Memory 列表、频段按钮最小可点边长 ≥ 48dp；
  连点 5 次同一目标，命中 5 次（错点一次即记不通过）。
  **实测（440dpi ⇒ 1dp=2.75px）**：右侧大按键列（PTT/APRS/WSJT-X/FT8/SSTV/DTMF/VOICE/CW/SET/MEM）
  横屏高 93px = **33.8dp**，竖屏高 117px = **42.5dp**，两档都低于 48dp。代码里这些按钮写的是
  `setMinimumHeight(64)`（[MainWindow.cpp:488](../apps/desktop/app/MainWindow.cpp#L488) 的 `mk_big`），
  手机上实际拿不到 64dp ⇒ 布局在纵向预算不够时**先把最小尺寸压掉**，这也正是 C1 竖屏叠列的成因。
  连点命中率没单独测（横屏大按键都还在屏内，误点风险主要在 C1 说的竖屏叠列上）。

- [x] **C3 旋转不重启进程** —— 真机过（2026-10-31）
  代码事实：manifest `android:configChanges` 已含 `orientation|screenSize|density|…`
  （值 `0x40003ff7`），`android:screenOrientation="unspecified"`。
  命令：`pid_before=$("$ADB" shell pidof org.radiosuite.app)` → 转屏 → 再取。
  判定：pid 不变，且频谱/解码表内容延续。pid 变了 = configChanges 没兜住，属真缺陷。
  **手段坑**：`settings put system user_rotation 3` 转不动这个应用（Qt 侧没跟着转），
  要验转屏得物理转手机，或者先开 `accelerometer_rotation 1`。测完记得把两个值改回原样
  （这台机器原来是 `accelerometer_rotation=0`、`user_rotation=0`）。

- [ ] **C4 软键盘输入**
  操作：点频率框 / FT8 文本框，让软键盘起来。
  判定：输入框不被键盘遮住（遮了就是必须滚动的布局缺陷）；能输入、能回删；
  按「完成/回车」不触发误发射。

- [x] **C5 独立面板窗口** —— 真机过（2026-10-31，FT8 / CW / APRS 三个面板）
  代码事实：APRS / WSJT-X / HamLog 工作台 / FT8 面板都是独立顶层窗口（父为空）。
  Android 上一个进程只有一个 Activity，多顶层窗口通常退化成一个全屏 SurfaceView。
  判定：四个面板都能打开、能操作、返回键只关面板不关主窗；
  **打不开或一开就盖死主窗**→ 记为安卓端结构性缺陷，不是可绕过的小毛病。
  **实测**：FT8 面板打开后能选音源、能读状态行（K4 的 D1 就是靠它验的）；CW 面板打开后
  键速/侧音/回读都在；APRS 面板打开后给的是真话「APRS 保存失败：Qt SQLite 驱动不可用」。
  `keyevent 4`（BACK）只关面板，主窗随后仍可 dump 到、进程不死。
  取证手段注意：`uiautomator dump` **只报最上层窗口**，判「面板还开着没」要认面板里独有的
  文本，别拿「有没有节点」当判据。WSJT-X 工作台与 HamLog 没单独点开。

- [ ] **C6 深色底 + 小屏可读性**
  判定：截屏里瀑布、S 表读数、状态行不叠字（桌面 2026-10-29 就抓到过右半屏越界读与表针压字，
  手机缩放后同类问题更易复现）。

---

## 4. Gate D — 持久化（Android 上是重灾区）

- [x] **D1 QSettings 到底存不存** —— **真机过了**（2026-10-31，证据见 K4）
  代码事实（改动前）：全应用有 **16 处**各写各的 QSettings——10 处**默认构造**
  `QSettings st;`（org/app 靠 [main.cpp:19-21](../apps/desktop/app/main.cpp#L19-L21) 设成
  `radio-suite`）：
  [MainWindow.cpp:119/446/473/481/1167/1179/1219](../apps/desktop/app/MainWindow.cpp#L119)、
  [wsjtx_panel.cpp:233/441/479](../apps/desktop/app/wsjtx_panel.cpp#L233)；
  另有 6 处 `QSettings st(kOrg, "radio-suite")`
  （[font_proxy_settings.cpp:47/56/65/91/105/119](../apps/desktop/ui_common/font_proxy_settings.cpp#L47)）——
  显式给了名字，但格式仍是 `NativeFormat`，Android 没有原生注册表/plist 后端。
  **代码侧已统一**（`fix/android-settings-persistence`）：这 16 处加上链路偏好那两处，
  现在都走 [ui_common/app_settings.h](../apps/desktop/ui_common/app_settings.h) 里唯一的
  `radio::ui::appSettings()`。
  **但统一之后真机仍然一个 ini 都不落**——第一版把它修成 `IniFormat/UserScope` 还是不行，
  因为 Android 上「看起来能写」的落盘点实际有三个，只有一个真能用：

  1. 默认构造的 `NativeFormat`：安卓没有后端，`setValue` 不报错，重启全是默认值（原始缺陷）。
  2. `IniFormat/UserScope`：应用进程的环境里**没有 `HOME`，也没有 `XDG_CONFIG_HOME`**
     （`su -c 'cat /proc/<pid>/environ' | tr '\0' '\n'` 里查不到），UserScope 解析出一条
     不存在的路径，`sync()` 写到一个不存在的目录里，**同样静默**。
  3. `QCoreApplication::applicationDirPath()`：看着是「应用自己的目录」，实际是 **APK 解出来的
     native 库目录** `/data/app/~~xxx==/org.radiosuite.app-yyy==/lib/arm64`，root 属主 755，
     应用连临时文件都建不出来；QSettings 存 ini 走「同目录建临时文件再 rename」的原子替换，
     所以必然失败——而且**失败不吭声**，logcat 里连一条 QSettings 警告都没有。
     这一条是踩过才知道的：把 ini 手动塞进 lib 目录（还要 `chcon` 补上 SELinux 的 MLS 类别，
     否则应用读不到）能读回 `transport: udp · 10.0.0.9`，看起来「路径对了」，其实只读不写。

  现在用的是第 4 个：`QStandardPaths::AppDataLocation`，拿不到再退 `QDir::currentPath()`
  ——Qt 启动时把工作目录设成应用自己的 files 目录（`/proc/<pid>/cwd` 就是它），两者其实同源，
  逐个试 `mkpath` + `isWritable()` 取第一个真能写的。启动日志会打实际落点
  （[main.cpp:98-102](../apps/desktop/app/main.cpp#L98-L102)），设备上排查这类「静默丢失」
  没有这行日志就只能靠猜。
  实测结果：`/data/user/0/org.radiosuite.app/files/radio-suite.ini`，`[link]`/`[mem]`/`[ft8]`
  三组都写进去了，force-stop 重启后 FT8 音源与链路偏好都读回来了。
  判定收窄：**「三处重启后仍在」= 通过**，这一轮 FT8 音源 + 链路两处真读到，`[mem]` 只验到
  「文件里还在、重启没丢」（读回要看主窗频率显示，而 UDP 链路连不上电台时显示恒 0，
  拿它当判据会假阴性）。音频设备那一处没单独验：这台包里没有可用的 RtAudio 后端。
  顺手对照：同一次重启里**链路设置应当保住**——已保住，见 K4 的 G-0。
  辅助命令（release 包**不能用 `run-as`**，本机是 root，直接 `su -c`）：
  ```bash
  "$ADB" shell 'su -c "ls -l /data/user/0/org.radiosuite.app/files/; cat /data/user/0/org.radiosuite.app/files/radio-suite.ini"'
  "$ADB" logcat -d --pid=$("$ADB" shell pidof -s org.radiosuite.app) | grep -E "radio-suite|QSettings"
  ```
  桌面等价验证（2026-10-31，非手机）：往 `HKCU\Software\radio-suite\radio-suite\font`
  写 `family=Consolas` / `size=16` → 不带任何参数起 radio-suite → 全界面字体变成
  Consolas 16（截图对照过），删键复原。这证明**改过的 `applySaved()` 仍从同一存储读到值**；
  它不证明 Android 的 ini 落盘，D1 照跑不误。
  顺带挖到的坑（**不是**本次改动引入，桌面实测）：手工 `reg add` 写出的
  `wsjtx/group`（值名带斜杠、平铺在 app 键下）**应用读不到**——QSettings 把
  `wsjtx/group` 理解成「组 `wsjtx` 里的键 `group`」，得落在子键 `wsjtx` 下才算。
  本机就留着这么一对平铺值（`wsjtx/group=127.0.0.1`、`wsjtx/port=2237`），
  所以 WSJT-X 面板照旧按默认组播 224.0.0.1 去 join 并报「加入组播失败」。
  要改监听地址请走面板里的「设置」，别用 `reg add`。

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

## 7. Gate G — 真机链路

前置三选一里**第 2 条已落地**（`fix/android-link-prefs`：`QSettings` 偏好 + 启动时决定
transport，见 K1）。第 1 条也顺带做了（对话框真返回表单内容，只是不热切换——存了要重启）。
第 3 条（A8 打包期钉参数）没做，仍可作为演示包的第二通道。
所以下面六条**不再因 K1 阻塞**，只等手机接上。

- [x] **G-0 应用内配置链路**（本节入口，手机上先做这条）—— **配置半条已过**（2026-10-31，见 K4）
  操作：主窗右上「连接…」→ 链路选「C++ 原生 UDP」→ 填电台 IP / 用户名 / 密码 → 保存
  → `"$ADB" shell am force-stop org.radiosuite.app` → 重新 `am start`。
  判定：状态栏空闲文本变成 `transport: udp · <你填的 IP>`（见 K3，这只证明程序按偏好
  建了 UDP 链路，通没通看 G-1 起的行为条目）；
  下拉框在手机上**只有两档**（没有 Python 桥那档，`bridgeAvailable()` 决定）；
  口令框不回显但重开对话框仍在（D1 里链路这一项已走显式 ini）。
  填错 IP 保存后重启应表现为**连不上/超时**，不是悄悄跑 mock——若状态栏又写回 mock，
  说明 IP 被折回空（launch_config 的降级判定），属真缺陷。
  **实跑结论**：三条判据全中——两档下拉、保存写 `[link] mode=1`、重启后
  `链路： udp （来源： 持久化偏好 ）` + `transport: udp · 192.168.0.31`；故意填了这台机上
  不存在的 IP，logcat 报 `transport open failed: "ctrl 信道无法连接 …"`、主窗 `0.000000 MHz`，
  **没有折回 mock**。重开对话框下拉框回填 UDP，不是回到默认。
  剩下的「真连上电台」要靠 G-1…G-5，而这需要先有一台 IC-705 在同一个网段里。

- [ ] **G-1** 行为判据：mock 的频率会自漂、PTT 自己走完时序；真机链路不会自漂。
- [ ] **G-2** 读回频率与电台面板一致（差 0 Hz）。
- [ ] **G-3** 写频往返：应用设 14.074.000 → 电台面板同值。
- [ ] **G-4** 50003 下行音频进频谱/瀑布，FT8 音源选「电台链路」能出解码行。
- [ ] **G-5** PTT 一键发射在真电台上出声，且应用状态回 RX。
- [ ] **G-6** 断链（拔网线/电台关机）后 UI 如实显示失联，不谎报在线。
  已知会误判的地方（桌面实测，2026-10-31）：频率会落到 `0.000000 MHz`、S 表 `-999 dBm`
  （如实）；但**瀑布/频谱照旧画三个合成峰**——`MainWindow.cpp:1242-1250` 只在网络音频
  active 时才切真实 PCM，与链路类型无关。看这条时**别拿瀑布当在线判据**，
  也别把假峰记成「链路正常」。

以上七条（G-0…G-6）**一条都不许靠 mock 行为打勾**——那会写出假报告。G-0 过不了就说明
偏好在安卓上没落地，回头查 D1 的 `run-as` 那条命令，别往下猜。

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
