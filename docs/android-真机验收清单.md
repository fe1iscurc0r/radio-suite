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
同一天又出了**第二包**（多一条 `065ff83`：APK 里打进 QSQLITE 驱动），`pm install -r` 覆盖安装，
D1 的 `radio-suite.ini` 原样保住（102 字节，13:46 那份），D3 的结论出自第二包。
**本清单的状态:** 2026-10-31 手机第一次接上并跑了 K4 那批条目（装包、D1 持久化、G-0 的
「保存→重启→按偏好建链路」这半条、K3 的离线不谎报），当夜又跑了 C3/C5/C6/C7/C8/C9
（锁横屏、独立面板、分页重排、制式互斥、HamLog 不可用）。11-01 凌晨按用户报的一串手机
缺陷修了 C10（对话框/面板能滚能关、波段高亮）；当天 13:30 手机解锁后按用户原话逐条复验，
C10 全部通过，并补了两个新根因的修复（下拉弹窗触摸错位、第二顶层窗口丢帧），
15:28 又补点了 SET/MEM/CW/DTMF/VOICE 五个对话框（同样全屏贴边、能滚、能关），
结论都写在 C10 正文里；17:46 把 C9 的待办落地（Android 自建本地 HamLog 库），
「日志」页签在手机上出得来、记 QSO 真写进库，17:53 顺带验了同页的 ADIF 导出
（走系统 SAF 选择器，`content://` 直写成功）。**其余每一条仍是
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
- **B1 + B4 麦克风这一路**：RECORD_AUDIO 运行时弹窗授权过（重启后仍在），FT8 音源切「本机麦克风」
  后状态行走的是 `fedSamples()!=0` 那个分支（「本机麦克风 · 已解 0 槽」，不是「设备已打开但没进
  样本」），进程里 `AudioRecord` 线程在吃 CPU。见 B4。
- **A7 参数注入**：release 非 debuggable 包上 `am start --es applicationArguments …` 真把参数
  送进了 `QApplication::arguments`，且优先级在偏好之上（当时偏好是 mock，起来却是 udp）。见 A7。

- **D3（写侧已验，读侧仍空）**：第一包给的是真话——状态栏「APRS 保存失败：Qt SQLite 驱动
  不可用」，因为包里没打 QSQLITE 插件，不是假成功。顺着这句查到根因在打包（`libQt6Sql.so`
  在 APK 里、`plugins/sqldrivers/` 不在），`065ff83` 把驱动挂到可执行目标上重出第二包后，
  应用自己建出 36864 字节的 `aprs.db`（`SQLite format 3` + 四张表）。
  「有站点 → 重启 → 轨迹还在」还没做到：顶行「连接 APRS-IS」在竖屏被裁到屏外，点不到，
  库里 0 条数据。详见 D3 正文。

**没跑、也跑不了的：**

- G-1…G-5（真电台的行为判据）：**手上没有可连的电台**，只有全盛 UVK6（不是 IC-705，
  没有 50003 音频口）。所以「手机上点保存 → 重启 → 真连上电台」这条只到「按偏好建出了
  UDP 链路并且老实报连不上」为止。
- **B3**（AAudio 真出声）：竖屏下 FT8 面板的发射按钮在屏外，没人把手机横过来就点不到。
- B5/B6（首帧延迟、半双工抢占）、Gate E（生命周期/音频焦点）、Gate F（FT8 时间基准）、
  A8（打包期钉参数——A7 已经通了，这条退路暂时用不上）、D2/D4（HamLog 路径、卸载即清）
  ——这一轮没有覆盖。

** adb 驱动这台手机的六个坑（下次照清单跑之前先看这段）：**

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
6. **`adb shell … cat` 取二进制文件会做 CRLF 转换**：`adb shell su -c 'cat …/aprs.db' > 本地`
   拿到的比设备上多 4 字节（每个 `\n` 前插了 `\r`），拿去算 md5 会「对不上」而误判取证失败。
   对账要用设备侧 `md5sum` 与「本地那份把 `\r\n` 还原成 `\n` 之后」比；或者用
   `adb exec-out`（本机的 `su` 仍会开 PTY，稳妥起见两条都做一遍）。


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

- [x] **A7 参数注入通道是否真通（K2 的实测）** —— 真机过了（2026-10-31，release 非 debuggable 包）
  命令：
  ```bash
  "$ADB" shell am start -n org.radiosuite.app/org.qtproject.qt.android.bindings.QtActivity \
    --es applicationArguments "--radio 192.168.0.31 --user linnan --pass <现场填>"
  ```
  **实测**（填的是 `--radio 10.0.0.9 --user probe --pass secret`，一个这里不存在的 IP）：
  logcat 三条连起来就是完整证据链——
  `链路： udp （来源： 命令行 ）` → `设置文件： "/data/user/0/org.radiosuite.app/files/radio-suite.ini"`
  → `W transport open failed: "ctrl 信道无法连接 10.0.0.9"`。
  参数真进了 `QApplication::arguments`，优先级也真在偏好之上（当时偏好是 mock），
  而且**没有**被降级判定折回。这条通道不需要 root、不需要 debuggable。
  一个取证坑：`am start` 之后立刻 `logcat -d --pid=$(pidof -s …)` 有概率取到空——Qt 还没打完
  那几行。别当成「注入没生效」，隔两秒重取，或者不带 `--pid` 直接 `logcat -d | grep radio-suite`。
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

- [ ] **B3 AAudio 输出真实出声** —— 仍未验（竖屏够不到触发按钮，见下面「实跑障碍」）
  操作：FT8 面板点「定时发射」（整槽 15 s），或先用 PC 侧同一套渲染逻辑做对照。
  命令：
  ```bash
  "$ADB" shell dumpsys media.audio_flinger | grep -iE "AAudio|underrun|power state" | head -20
  ```
  判定：听得到 15 s 的 FT8 音（约 1500 Hz 一组 tones）；`pidof` 不变；
  underrun 计数不持续上涨。若完全静音，先分清是没权限/设备号错，还是 `AudioOut` 在
  Android 只回 `kDefaultDeviceId` 这条已知简化的后果。
  **实跑障碍**：竖屏下 FT8 面板的「任意时隙」「定时发射」两个按钮 bounds 都是 `[0,0][0,0]`
  （面板自己的宽度也超出 1080，和 C1 主窗同一类毛病），所以这条必须在横屏点。手机放在桌上
  没法物理转屏，而 `settings put system user_rotation` 又转不动 Qt（见 C3）——**要人把手机横过来
  一次**，或者等 C1 的布局决定落地。

- [x] **B4 AAudio 采集真拿到样本** —— 真机过了（2026-10-31）
  操作：FT8 面板音源选「本机麦克风」→ 开始解码。
  判定：面板状态行从「等待…」进到「解码中…」，并且**已解槽数每 15 s 递增**。
  代码依据：`SlotDecoder::fedSamples()` 区分「对齐等待」与「真没音频」，所以这条一旦显示
  「已解 N 槽」就是真收到样本，不是界面话术。
  **原判据要改一句**：「已解 N 槽递增」需要房间里真有 FT8 信号，安静环境下必然一直是 0，
  拿它当通过条件会假阴性。真正的判据是状态行走哪一个分支
  （[ft8_panel.cpp:339-344](../apps/desktop/app/ft8_panel.cpp#L339-L344)）：
  `fedSamples()==0` 时写的是「等待麦克风采样（设备已打开但没进样本）」，非 0 时才写
  「本机麦克风 · 已解 N 槽」。
  **实测**：状态行是「本机麦克风 · 已解 0 槽」——即 `fedSamples() != 0`，样本真进来了。
  独立对照：应用进程里有个名为 `AudioRecord` 的线程，4 s 内 utime 从 218 涨到 223 jiffies
  （在吃 CPU，不是挂着不动）：
  ```bash
  T=$(for t in $("$ADB" shell ls /proc/$(pidof -s org.radiosuite.app)/task); do \
        "$ADB" shell su -c "grep -q AudioRecord /proc/*/task/$t/comm" 2>/dev/null && echo $t; done | head -1)
  ```
  触发方式记一下：面板是从右侧「FT8 本机」大按键开出来的，`toggled` 里就有一句
  `if (!decoding()) startDecoding()`（[MainWindow.cpp:638](../apps/desktop/app/MainWindow.cpp#L638)），
  而 `startDecoding()` 在麦克风音源下**开不起采集就直接 return**
  （[ft8_panel.cpp:147](../apps/desktop/app/ft8_panel.cpp#L147)），按钮不会变成「停止解码」——
  所以「按钮写着停止解码」本身也是采集打开成功的一个旁证。

- [ ] **B5 首帧延迟与稳态预算**
  操作：起 FT8 麦克风解码后**先让它跑 1 s 预热**，再连续跑 ≥ 4 个时隙（60 s）观察。
  判定：「已解」计数每 15 s 稳定 +1、不跳槽；首个解码结果在 start 后 ≤ 2 s 内出现属正常
  （对应桌面那段启动开销）。跳槽 = 真欠载，记进 §9 并按机型注明，不要顺手调阈值。

- [ ] **B6 半双工抢占如实拒绝**
  操作：同时打开网络音频（50003）和 FT8 麦克风音源。
  判定：后开的一方**明确拒绝**并给出原因，而不是两个都"在跑"却只有一个出声
  （桌面侧这条已有实现与用例，Android 只是重验设备枚举差异）。

---

## 2b. 权限矩阵（麦克风 / 存储 / USB OTG / 组播）—— 2026-11-01 核对

用户提的「手机端还要询问权限：OTG、存储等」，逐条查实。**这一节是静态核对 + 一次真机
`dumpsys`（18:40），OTG 那条今天证不了**，原因写在下面，不假装做过。

取证据的命令（一次跑完，别手搓）：
```bash
AAPT=/d/Android/sdk/build-tools/34.0.0/aapt2.exe
"$AAPT" dump permissions "<APK>"   # 包里真正声明了什么
"$AAPT" dump badging   "<APK>" | grep -iE "feature|targetSdk"
"$ADB" shell dumpsys package org.radiosuite.app | sed -n '/runtime permissions/,/installed/p'
ls /d/Qt/6.7.3/android_arm64_v8a/include/QtSerialPort   # 安卓 Qt 有没有串口模块
grep Qt6SerialPort_DIR build-android/CMakeCache.txt
```

| 权限 / 特性 | 谁声明的 | 真机状态（11-01 18:40） | 结论 |
| --- | --- | --- | --- |
| `RECORD_AUDIO` | 我们（`packaging/android/AndroidManifest.xml:11`） | `granted=true` | 启动时按 B1 申请，行为对 |
| `INTERNET` / `ACCESS_NETWORK_STATE` | androiddeployqt 注入 | 安装期权限 | 无需弹窗 |
| `WRITE_EXTERNAL_STORAGE`（隐含 `READ_`） | **Qt6Core 的 `lib/Qt6Core_arm64-v8a-android-dependencies.xml` 注入，不是我们写的** | 两条都 `granted=false`（`RESTRICTION_INSTALLER_EXEMPT`） | targetSdk 34 上这是**死权限**，见下 |
| `CHANGE_WIFI_MULTICAST_STATE` | 我们（`packaging/android/AndroidManifest.xml`，11-01 补） | 未复测 | normal 权限，装机即授；锁已在代码里取（见下） |
| `android.hardware.usb.host` / USB 权限 | 未声明（包里只有 microphone[not-required] + 两条 implied） | — | 缺口不在弹窗，在后端 |

**存储：不该问，也问不出东西。** Android 13 / targetSdk 34 已经没有「存储」这个运行时权限，
`WRITE_EXTERNAL_STORAGE` 是 Qt Core 依赖清单带进来的历史遗留，系统直接不给授（真机
`granted=false`），而应用一点没受影响——写路径本来就不靠它：私有目录一直在写
（`radio-suite.ini`、`aprs.db`、11-01 新建的 `HamLog/Log.db`），对外导出走 SAF
（17:53 ADIF 导出实测落 `/sdcard/Download/radio-suite.adi`，全程没弹任何权限框）。
再往上只剩 `MANAGE_EXTERNAL_STORAGE`（所有文件访问），那是系统安全设置、MIUI 还要单独放行——
**不申请**，一个电台面板不该要整机的文件系统。

**USB OTG：真正缺的是后端，不是弹窗。** 安卓版 Qt 6.7.3 **不带 QtSerialPort**
（`include/QtSerialPort` 不存在、`Qt6SerialPort_DIR-NOTFOUND`），所以
`RADIO_SUITE_HAVE_SERIALPORT` 在安卓构建里是关的，APRS 面板的串口入口走的是
`apps/desktop/app/aprs_panel.cpp:847` 那句降级「本构建未启用串口（缺少 Qt6::SerialPort），
请用 KISS TCP」。要做 OTG 串口得自己接 `UsbManager`：JNI + 清单加
`<uses-feature android:name="android.hardware.usb.host">` + `USB_DEVICE_ATTACHED` 的
`device_filter.xml` + `UsbManager.requestPermission()`（USB 授权不是运行时权限组，只能这么要），
或 vendor `usb-serial-for-android`（Apache-2.0）。**今天证不了**：手机唯一的 USB 口被 adb
占着，台架上也没有可插的串口设备/电台；只加清单声明、不加后端是自欺，所以整条按工单排。

**组播：只声明权限不拿锁等于没做——这轮把锁补上了，但真机还没验。** WSJT-X 面板默认监听
`224.0.0.1:2237`（`apps/desktop/app/wsjtx_client.cpp` `joinMulticastGroup`），Android 上不持
`WifiManager.MulticastLock` 时组播帧一般收不到（**join 会返回成功，一个包都不来**，所以这条
缺陷的表象是「没数据」而不是「报错」）。11-01 深夜代码落地：新增
`apps/desktop/app/multicast_lock.{h,cpp}`（JNI 走
`QNativeInterface::QAndroidApplication::context()` → `getSystemService("wifi")` →
`createMulticastLock(tag)`，`setReferenceCounted(false)` 后 `acquire()`），
`WsjtxClient::start()` 在 `joinMulticastGroup` **之前**取锁、`stop()` 释放，取锁失败时把原因
写进错误串并提示改单播；manifest 补 `CHANGE_WIFI_MULTICAST_STATE`（新包
`aapt2 dump permissions` 实测已在）。非 Android 上这类是空实现且 `held()` 保持 false，
不会假装持有。**未验的部分**：真机上锁是否真的拿到（`SecurityException` 路径没跑过）、
拿到后组播帧是否真的进来——见下面判据。单播不受影响：把地址指成手机可达的单播地址就能
先证数据通路——**这条是能做的，且不需要电台**。

判据（下次真机补，别照抄本节结论）：
- 新包 `aapt2 dump permissions` 的期望集合（11-01 构建后实测这七项）：`RECORD_AUDIO`、
  **`CHANGE_WIFI_MULTICAST_STATE`**、`INTERNET`、`ACCESS_NETWORK_STATE`、
  `WRITE_EXTERNAL_STORAGE`、`READ_EXTERNAL_STORAGE`、
  `org.radiosuite.app.DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION`。前两条是我们自己声明的，
  其余都要能说出谁注入的；对不上就当场查，别写「大概」；
- 撤销 `RECORD_AUDIO` 后 FT8 选「本机麦克风」要如实失败（B1 的撤销分支至今没走过）；
- 若做了 OTG：插上设备弹 `UsbManager` 授权框，点拒绝后面板如实报错、不崩；
- **组播锁（代码已落地，真机两条分支都要走）**：① `logcat` 里不该出现
  `SecurityException ... CHANGE_WIFI_MULTICAST_STATE`；WSJT-X 面板按默认组播启动后不报错；
  ② 同一网段 PC 发 Decode 包 → 手机面板出解码行（这才是锁真起作用的证据；只「没报错」
  不算，因为不持锁时 join 也成功）；③ 对照：同机器改单播地址仍能出解码行。

---

**台账已经进 app（11-01 深夜）**：面板页新增「自检 / 台账」大按键
（`apps/desktop/app/self_check_dialog.cpp` + `device_report.cpp`），一屏列出设备、权限、
电台链路、WSJT-X、音频、数据六组的**现场事实**与降级原因，可刷新，Android 上还带一个
「申请麦克风」出口（被永久拒绝时给的是去系统设置的路径，不是死路）。
它只报读到的事实：判定与措辞都在 `buildDeviceReport()` 这个纯函数里，所以**安卓的说法
在桌面上就能跑断言**（`tests/test_device_report.cpp`，10 条）。
⚠️ 别拿这页当验收结论——它证明的是「程序读到了什么」，不是「用户真收到了什么」：
比如组播那行显示「已持有」只说明 JNI 调用没抛异常，**收到没收到包仍要按上面第 ② 条
用 PC 发包证**。

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
  **拍板结果（2026-10-31，用户自己拿手机跑过一轮之后）**：竖屏放弃，横屏保留 ——
  「固定横屏的话效果会好很多」「反正竖屏是别想了，横屏还是可能的」，所以 ① 直接采纳
  （manifest 现在还是 `screenOrientation="unspecified"`，见 C3）。但 ② 也要做，理由不是
  竖屏而是**横屏仍然不适配手机**：状态行「Transport UDP」把频谱的高度吃掉一截、
  按键偏大整屏偏小（这是手机端不是平板端）、**S 表压住 40m 波段那一行、调谐旋钮被波段按钮盖掉、
  还有按钮直接消失**。方向是分页：频谱单独一页、主页只显示当前波段且点开进波段调谐页、
  制式（LSB/USB/AM/CW/FM/WFM）单独一页或一个入口，别把全部控件挤在一屏。
  这一轮另有一条同源的实缺陷，见 C7。
  **横屏实测（锁横屏的包，`dumpsys` 给 `app=2320x1036` = 843×376dp，2026-10-31 21:34 截图）**：
  桌面那套两列布局在 376dp 纵向预算里**不是挤，是互相盖**。uiautomator bounds 直接量到：
  功能开关行 `ATT=[97,597][250,690]` 与 `NB=[212,597][365,690]` **横向重叠 38px**；
  旋钮那一列被压成 `步进 1 kHz` 行高 **43px**、`▼升频/▲升频/频率框/GO` 行高 **27px**
  且四个元素两两重叠 117px（截图里它们缩成旋钮上方一条糊掉的橙黄色带）；
  频段行 `[97,297][245,390]` 起在 S 表盘面之上（S 表被 40m/20m 压住），
  调谐旋钮下半被「双 VFO」行盖住；右列 11 个大按键每个只有 ~93px 高，
  两行中文标题**上下各切掉一半**（「APRS 台流」「FT8 本机」「CW 键控」都是残字）；
  底行「网络合…」「音频设…」被裁到屏幕右缘外。频谱只有顶部 ~60px 是波形、
  瀑布只剩最底下一条，而它下面还压着 `transport: mock · radio: IC-705 sim` 那一行。
  **结论：横屏能点、能读，但一屏塞桌面全量控件这条路在手机上不成立**，分页是必需的。

- [ ] **C2 触摸目标 ≥ 48dp** —— **真机不通过**（2026-10-31 实测）
  命令：`"$ADB" shell wm density` 取 `densityDPI`，按 `dp = px * 160 / densityDPI` 换算。
  判定：旋钮、CW Set Mode 树、Memory 列表、频段按钮最小可点边长 ≥ 48dp；
  连点 5 次同一目标，命中 5 次（错点一次即记不通过）。
  **实测（440dpi ⇒ 1dp=2.75px）**：右侧大按键列（PTT/APRS/WSJT-X/FT8/SSTV/DTMF/VOICE/CW/SET/MEM）
  横屏高 93px = **33.8dp**，竖屏高 117px = **42.5dp**，两档都低于 48dp。代码里这些按钮写的是
  `setMinimumHeight(64)`（[MainWindow.cpp:488](../apps/desktop/app/MainWindow.cpp#L488) 的 `mk_big`），
  手机上实际拿不到 64dp ⇒ 布局在纵向预算不够时**先把最小尺寸压掉**，这也正是 C1 竖屏叠列的成因。
  连点命中率没单独测（横屏大按键都还在屏内，误点风险主要在 C1 说的竖屏叠列上）。
  **分页版（2026-10-31 夜）**：所有页签按钮与页内按钮统一抬起最小高度 **44dp**
  （`findChildren<QPushButton*>` 扫一遍，低于 44dp 的抬到 44dp）。44 不是 48：
  376dp 的纵向预算里再要 48dp 会把瀑布挤没，**这条按 48dp 的判定仍未通过**，
  记作有意识的折中，真机触摸命中没连点测过（页面元素不再重叠，误点风险主要随 C1 一起消失）。

- [x] **C3 旋转不重启进程** —— 真机过（2026-10-31）
  代码事实：manifest `android:configChanges` 已含 `orientation|screenSize|density|…`
  （值 `0x40003ff7`），`android:screenOrientation="unspecified"`。
  命令：`pid_before=$("$ADB" shell pidof org.radiosuite.app)` → 转屏 → 再取。
  判定：pid 不变，且频谱/解码表内容延续。pid 变了 = configChanges 没兜住，属真缺陷。
  **手段坑**：`settings put system user_rotation 3` 转不动这个应用（Qt 侧没跟着转），
  要验转屏得物理转手机，或者先开 `accelerometer_rotation 1`。测完记得把两个值改回原样
  （这台机器原来是 `accelerometer_rotation=0`、`user_rotation=0`）。
  **锁横屏之后复跑（`de3638e` 的包，2026-10-31 21:34）**：`user_rotation=0` +
  `accelerometer_rotation=0`（= 手机竖着拿、自动旋转关着）下起应用，
  `dumpsys window displays` 给的是 `mDisplayRotation=ROTATION_90`、`cur=2400x1080`、
  `app=2320x1036`、config 里带 `land` ⇒ `sensorLandscape` 真的生效，不靠手机物理姿态。
  可用绘制区 **843×376dp**（440dpi），这就是分页重设计要塞进去的纵向预算。

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

- [x] **C6 深色底 + 小屏可读性** —— 真机过了（2026-10-31 夜，分页版本，见 C8）
  判定：截屏里瀑布、S 表读数、状态行不叠字（桌面 2026-10-29 就抓到过右半屏越界读与表针压字，
  手机缩放后同类问题更易复现）。
  **实测（2026-10-31 用户亲测，横屏）**：状态行「Transport UDP」占掉频谱纵向预算、
  S 表与 40m 波段行叠字、调谐旋钮被波段按钮盖住、部分按钮直接不在屏内 —— 不通过，
  与 C1 同一批修（分页重排）。
  **分页版复跑**：状态行进顶栏不再占频谱高度；频谱页只剩 S 表/SWR 一行 + 整宽瀑布，
  无叠字；调谐/制式页各占一屏、控件不再互相覆盖。逐页截图见 C8 的证据段。

- [x] **C7 制式按钮互斥** —— 真机报缺陷，已修（2026-10-31，`fix/mode-button-exclusivity`）
  用户报：「不知道为什么 LSB、USB、AM、CW、FM、WFM 可以同时开启，这个问题很奇怪」。
  代码事实（修前）：[MainWindow.cpp:853](../apps/desktop/app/MainWindow.cpp#L853) 六个按钮只
  `setCheckable(true)`，点下就 `setChecked(true)`，**唯一的取消路径**是
  `syncTogglesFromState()`，而它只在有回读帧真的改了状态时才跑
  （[session.cpp:226](../apps/desktop/app/session.cpp#L226) 末尾的 `emit stateChanged()`）。
  手机选 UDP 链路而电台不应答（关机 / IP 填错，K4 那台就是这个状态）⇒ 一帧回读都没有，
  点几个亮几个且永不纠偏。互斥是控件层的不变量，不该外包给网络：改 `setAutoExclusive(true)`。
  同一个循环里还查出第二条：`Mode` 枚举含 `RTTY=4` 而按钮行没有 RTTY，回读却拿**数组下标**
  当枚举值比 ⇒ FM(5) 的回读点亮第 6 个按钮（写着 WFM）、WFM(6) 谁都不点、RTTY 全灭。
  改成按钮自带制式值（`mode_vals_`）按值比。
  **验证（桌面实窗，UIA 读选中态；MainWindow 不在任何测试目标里，所以只有实窗证据）**：
  `--radio 127.0.0.1`（无应答）连点 LSB→AM→FM，这组按钮在 a11y 里已变成**单选按钮**
  且每次只有一个 `selected`；`--mock` 点 FM 回读 `FM/0` 亮 FM、点 WFM 回读 `WFM/0` 亮 WFM。
  **真机复跑（带 `de3638e` 的包，mock 链路，横屏）**：`input tap` 点 FM (855,528) →
  `checked=['FM']` 且回读标签 `FM/0`；点 WFM (1025,528) → `checked=['WFM']`、`WFM/0`。
  互斥那条没在手机上单独复跑（手机这条链路会应答，回读本来就会纠偏；不应答的场景已在桌面上钉死）。
  **新取证通道（比 bounds 可靠得多）**：这台机上 Qt 把控件标签放在 uiautomator 的
  **`content-desc`**（`text=` 全空），而 `checkable` / `checked` 属性是真值 ——
  所以「按钮亮没亮」可以直接 `uiautomator dump` 读出来，不用猜像素。
  主窗控件的 bounds 在这台机上是准的（K4 记的 126px 右偏只出现在 QDialog）。

- [x] **C8 手机上分页重排（C1/C6 的修法）** —— 真机过了 5 页版本（2026-10-31 夜）
  用户方向（原话）：「如果你要调节频谱的话，你可以重新搞一个界面出来……然后其他的话，
  像制式的话，你也可以单独做一个制式按键出来」。落地方案：**只动 Android 布局**——
  桌面两列布局逐行保留（`buildDesktopUi()`），Android 走 `buildAndroidUi()`：
  顶部一行 = 频率大字 + 状态文本 + PTT + 连接…；中间 `QStackedWidget` 五页；
  底部一排可勾选页签（`QButtonGroup` 互斥）。
  页签内容：`频谱`（S 表/SWR + 全宽瀑布）、`调谐`（旋钮/步进/±/频率输入/10 个波段/VFO）、
  `制式`（六制式 + SPLX/中继 + 8 个开关 + RIT/ΔTX/SPLIT + M1 存取 + 音频）、
  `面板`（9 个大按键 3×3）、`日志`（HamLog，见下条）。调谐/制式两页套了 `QScrollArea`，
  保证 376dp 预算里点不到的东西能滚到，而不是被裁掉。
  控件只构造一次：桌面和安卓共用 `buildLcdBand/buildDeckWidget/buildBandsRow/…` 这组
  builder，`findChildren` 白名单不成立——每个控件各有一个成员指针，不存在两套实例。
  另把状态行从底部一行挪进顶栏（「Transport UDP 会占用频谱图的距离」的直接修法），
  手机顶栏的 `TX = RX` 只在 DUP−/DUP+ 时显示，simplex 不占宽。
  **真机证据（2026-10-31 23:5x，横屏 843×376dp，mock 链路）**：五页逐页截图核对——
  频谱页 = 顶栏 + S 表/Po-SWR 一行 + 整宽瀑布 + 五页签；调谐页 10 个波段按钮全在屏内、
  无叠字，旋钮不再被波段行盖住；制式页六制式 + 开关 + RIT + 存储 + 音频全部排进
  一屏可滚区域，USB 点亮唯一；面板页 3×3 大按键每行标题完整；日志页当时给的是
  「HamLog 不可用」的真话（见下）。C1/C6 那批「S 表被 40m 压住、旋钮被波段盖掉、
  按钮消失、状态行吃频谱」在分页版本里逐条不再复现。
  **构建证据**：MSVC 桌面 `radio-suite` 编译通过、`ctest` **181/181**；Android clang
  交叉编译通过（顺手修了一条 `lambda capture 's' is not used` 警告——只在 clang 报）。
  **未覆盖**：MainWindow 不在任何自动测试目标里，分页布局没有自动化回归，
  真机截图就是全部证据。桌面布局另做了一次实窗核对（`--mock`，1105×768 截图）：
  顶栏、S 表/旋钮/Po-SWR 同排、波段/VFO/制式/开关/RIT/存储/音频各一行、瀑布、QSO 表、
  右列大按键、底部 `transport: mock` 状态行全部与重构前一致——但这只是人眼对照，
  没有像素级基线。

- [x] **C9 HamLog 在 Android 上从未可用** —— 真机发现（2026-10-31 夜），已修，11-01 真机复验通过
  `RADIO_SUITE_HAVE_LOGBOOK=1` 确实编进了 Android 包（`build-android/build.ninja` 可查），
  但真机上 `hamlog_.available()` 恒为 false：`HamLogDb::defaultDbPath()` 指向的库文件
  在手机上不存在，而 `available()` 的契约是「库文件存在且 log 表可读」。
  后果：桌面右列那组「记 QSO / ADIF / 日志」在手机上一直是静默隐藏的（旧布局没有暴露），
  分页版第一稿把「日志」页做出来才看见它是一页死内容。
  **当前处置**：`hamlog_.available()` 为假时**不建这一页、不出这个页签**（手机上是四页签：
  频谱/调谐/制式/面板），宁可少一页也不摆一个点不开的死页面。
  **11-01 15:24 真机核对**：最终包（代码止于 `e8ccabb`，之后只改文档）面板页底部确为
  频谱/调谐/制式/面板 四个页签，日志页签不存在，与该处置一致；本轮截图只点了面板页，
  其余三页在 13:30 那轮点过。
  **待办**（提于 15:24，同日 17:46 已落地并复验，见下）：给 Android 定一个真正可写的 HamLog 库路径
  （或首次启动建库），再做手机上的记 QSO / ADIF。这条与 D3（QSQLITE 驱动）不同——驱动已经
  打进包了，缺的是库文件本身。
  **修法（`373d2a2`）**：`HamLogDb` 新增 `createDatabase()`（log 18 列 + settings 两张表，
  全部 `IF NOT EXISTS`——已有表和行一律不动，对 HamLog 自己的库重复调用也安全）和
  Android-only 的 `ensureLocalDb()`；`defaultDbPath()` 在 Android 上改走
  `radio::ui::settingsDir()`（本机已验证可写的 `QStandardPaths::AppDataLocation`），
  落点 `/data/data/org.radiosuite.app/files/HamLog/Log.db`；`MainWindow` 构造在 `buildUi()`
  **之前**调用建库，失败只 `qWarning`，不改「库拿不到就不出这一页」的处置。桌面路径不变。
  **11-01 17:46 真机复验**（Redmi K40，签名包与桌面同码）：冷启动后 `files/HamLog/Log.db`
  自动建出，从盘上读回的 DDL 确为 HamLog 的 18 列 `log` + `settings`；底部页签多出第五个
  「日志」，点进去页面正常渲染；在「对方呼号」输入 `BG1TEST` 点「记 QSO」，顶栏绿色状态
  `已写入 HamLog: BG1TEST @ 14.2700 MHz USB`，列表出现 `BG1TEST  14.2700 MHz USB  2026-11-01
  0946`，输入框回到占位文案，pull 回的库文件里 `BG1TEST` 命中 1 次。
  **17:53 同页「ADIF 导出」也验了**：`QFileDialog::getSaveFileName` 在 Android 上不是 Qt 自绘
  对话框——Qt 6.7.3 把它交给系统 SAF 选择器（`dumpsys window` 里 `mCurrentFocus` 直接变成
  `com.google.android.documentsui/com.android.documentsui.picker.PickActivity`），文件名框预填
  `radio-suite.adi`、「保存」可点；选完 `QFile` 直接往返回的 `content://` URI 写，
  `/sdcard/Download/radio-suite.adi` 落出 162 字节，逐字节是
  `<ADIF_VER:5>3.1.4<PROGRAMID:11>radio-suite<EOH>` + `<CALL:7>BG1TEST<FREQ:9>14.270000<QSO_DATE:8>20261101<TIME_ON:6>094600<MODE:3>USB<RST_SENT:2>59<RST_RCVD:2>59<EOR>`，
  顶栏回显「已导出 1 条 QSO → …」。**遗留小瑕疵（未修）**：那句回显把 URI 原样打出来，手机上
  看到的是一串 `content://com.android.provide…` 截断文本，用户看不出文件落在哪；改成人话
  （如「已导出 N 条 QSO → 下载/radio-suite.adi」）属产品口径，没擅自改。
  **注意**：手机上这份库现在留了一条测试记录 `BG1TEST`（id 1），`/sdcard/Download/radio-suite.adi`
  是本轮导出留下的 162 字节样本，两者都与真机通联无关，要清就直接删这两处。

- [x] **C10 对话框与独立面板「能滚、能关」** —— 用户真机报缺陷，已修，11-01 真机复验通过
  用户原话：「波段点击不显示亮度（在哪）音频设置没有退出键 cancel 无效，应用中继也
  没有关闭键，频差无法设置，模拟亚音模式无法设置，数字亚音未看到，估计无法滑动。
  电台连接关闭键失效，APRS，WSJTX 无退出键按钮切 wsjtx 页面切换有问题 SSTV 依旧
  无法滑动」。
  **根因（代码侧自查）**：Android 上没有窗口管理器，`QDialog` 与 `Qt::Window` 面板
  既没有标题栏也没有关闭按钮；而对话框尺寸不受屏幕约束（中继对话框 560 宽、
  音频设置 460 宽、SSTV 更高），376dp 高的内容区把底部「确定/取消」整排推到屏外，
  于是表现为「没有退出键 / cancel 无效 / 频差与亚音选不到 / 不能滚」。面板（APRS /
  WSJT-X / FT8）连关闭按钮都没有；系统返回键直接 hide 顶层窗，但主窗的大按键仍是
  checked，再点一下只是把「看不见的已开窗」关掉——就是「切 wsjtx 页面切换有问题」。
  波段按钮根本没有 `setCheckable`，点完没有任何回读高亮。
  **修法**：新增 `apps/desktop/app/touch_dialog.{h,cpp}`（Android-only 生效）：
  `fitDialog` 把对话框根布局整体搬进 `QScrollArea`，底部钉一条常驻「关闭」（→ reject），
  交互控件（按钮、下拉框、数字框、输入框、勾选）统一抬到 ≥44dp，窗口压进
  `QScreen::availableGeometry()`；滚动区挂
  `QScroller::grabGesture(viewport, LeftMouseButtonGesture)`——QScrollArea 默认只认滚轮
  和滚动条，手指拖动根本不会滚（安卓上触摸被合成成鼠标事件，所以选左键拖动这一档）；
  分页主窗的 `wrapScroll` 同样挂上，调谐/制式页才真能用手拖着滚。
  `addCloseBar` 给三个独立
  面板各插一条「标题 + 关闭 ✕」顶栏；`QMessageBox` 换成语义相同的可滚动信息框
  `showInfo`（同样常驻关闭，滚动区同样挂 QScroller）。接线覆盖：音频设置、中继/亚音、
  连接设置、SSTV、DTMF、语音存储、CW、SET、MEM 信道、APRS 消息、APRS RF 设置、
  WSJT-X UDP 设置。
  **装包后在手机上抓到第二个缺陷，已修（`f540c30`）**：第一次解锁复验时波段高亮已
  生效、音频设置对话框底部出现了常驻「关闭」，但对话框里「扬声器（RX 下行）」跑到了「取消/确定」
  下方、中间一大段空白，往里拖也不滚。根因在 `fitDialog` 搬子布局那一步：
  `QBoxLayout::takeAt()` 返回的 item **就是子布局对象本身**（Qt 6.8 `qboxlayout.cpp`
  的 `takeAt` 直接返回 `b->item`），`addLayout()` 收养它时已经 `setParent(page)` 并把
  里面的控件改父到滚动页；紧接着的 `delete it` 又把同一个布局删了——滚动页持悬空
  指针。真机上是这些控件停在旧坐标、不再参与布局；离屏回归用例能稳定复现
  （进程 `0xC0000409`）。修法：子布局分支不再 `delete it`，只交给 `addLayout` 收养
  （控件分支的 `QWidgetItem` 仍可安全删除，它是布局项而不是控件）。
  回归用例 [tests/test_touch_dialog.cpp](../tests/test_touch_dialog.cpp)：
  `fitDialog reparents form rows into scroll content` 断言表单标签/下拉框确实是滚动页
  的后代、纵向顺序「表单 < 分组框 < 按钮箱」、常驻关闭 ≥44dp；桌面靠
  `RADIO_SUITE_TOUCH_FIT=1` 打开同一套触摸层逻辑跑这组断言。
  主窗侧：三个面板事件过滤器收 `QEvent::Hide` → 复位对应大按键；三个面板大按键改为
  互斥（开一个收掉另两个）并给 `:checked` 琥珀背光；波段/广播预置按钮改 `checkable`，
  `syncBandHighlight()` 在 `refreshState()` 里按 `[low, high)` 回读点亮（40m 7.000–7.300、
  20m 14.000–14.350、15m 21.000–21.450、10m 28.000–29.700、6m 50–54、2m 144–148、
  70cm 430–440 MHz；AIR 118–137、BC-FM 87.5–108、BC-SW 9.4–9.9）。
  **已验**：桌面 MSVC 构建（all 目标，含三个离屏 UI 测试）通过、`ctest` **182/182**
  （含新增的 `fitDialog reparents form rows into scroll content`）；
  Android clang 交叉编译通过并
  出了新 APK（已 `adb install -r` 装到手机上，含后续补丁：输入控件触控高度 + `showInfo`
  滚动）；桌面实窗（`--mock`，1105×768）截图
  可见 14.070 MHz 时 **20m 亮琥珀**、其余频段暗色，与制式/步进按钮的选中态一致。
  注：`tests/CMakeLists.txt` 的三个 UI 测试目标原先没编 `touch_dialog.cpp`，
  Windows/macOS CI 在链接期报 `LNK2019 fitDialog`——已补源并重建验证；此前只构 app
  目标时 `ctest` 用的是旧测试二进制，这是本轮漏检点。
  **11-01 13:30 真机复验（已解锁，Redmi K40 / 873×393dp）：用户报的那串缺陷全部复测通过**
  - 频差：方向下拉三行（同频 SPLX / 下差 DUP− / 上差 DUP+）都能点中；选「下差 DUP−」
    → 应用频差 → TX 立刻变 `13.670000 MHz −600.0 kHz`；选「上差 DUP+」→
    `14.870000 MHz +600.0 kHz`，不需要再点别处才刷新。
  - 模拟/数字亚音：模式下拉列出全部 8 项（OFF / TONE / TSQL / DTCS / DTCS(T) /
    TONE(T)/DTCS(R) / DTCS(T)/TSQL(R) / TONE(T)/TSQL(R)），选 DTCS（数字亚音）后
    往下滚可见「码 023」「接收极性 / 发射极性」；应用模式后主窗中继键回读成
    `中继 DUP-...[DTCS]`。
  - 音频设置：Cancel/OK 都在屏内，Cancel 直接关闭；连接对话框、中继对话框的常驻
    「关闭」都生效。
  - 波段：14.270 时 20m 亮；点 40m 后 40m 亮、频率变 7.100000，调谐页拖动正常。
  - 面板：APRS / WSJT-X 都有「关闭 ✕」；WSJT-X 关掉再开、系统返回键收面板都正常，
    返回后面板大按键复位；SSTV 面板用 QScroller 拖得动。
  - 15:28 补点 SET / MEM / CW / DTMF / VOICE：五个对话框都按全屏贴边打开、底部常驻
    「关闭」一点即回面板页；SET 内容一屏放得下（滑动无位移、左侧标签完整）；MEM 滑到底
    可见 `名称`、`Scan/DATA/Tone` 与 读取/写入/选组/选信道；CW 滑到底可见回读提示与
    应用设置/发送；DTMF 滑到底可见「生成 .wav」（键盘 + 清空在首屏）。
  **第三个根因（下拉弹窗点不中）**：Qt 6.7 安卓顶层窗口的触摸换算把窗口位置按
  dp÷dpr 多减一次，只有窗口在 (0,0) 才准；而下拉弹窗永远贴在控件旁边（位置非零），
  于是点哪个选项都命中错行——用户看到的就是「模拟亚音模式无法设置、数字亚音未看到」。
  修法：`installPopupOriginFix()` 应用启动时装一个全局过滤器，`QComboBoxPrivateContainer`
  弹出后钉到 (0,0)（`show` 内还会再算一次几何，用 `singleShot(0)` 二次钉）；Qt 自己的
  itemSelected 在触摸合成鼠标事件的路径上不触发，另在列表 viewport 上装
  `PopupClickFilter` 按 `indexAt` 选行，`setCurrentIndex`/`hidePopup` 推迟到下一轮事件循环。
  **第四个根因（第二个顶层窗口丢帧）**：对话框里控件状态已变、新帧却合成不上屏
  （应用频差后 TX 标签迟一步、下拉选完不刷新、甩动结束回弹的横向偏移留在屏上要再点
  一下才回正）。修法：对话框内每次 `MouseButtonRelease` 后 60ms 补一次整窗
  `update()` + `QWindow::requestUpdate()`（`TapRepaint`）；`QScroller` 回到 `Inactive`
  后再补一帧；同时把滚动页横向范围钉死为 0，手指横向抖动不再把整页拖偏、切掉左侧标签。
  **仍未验**：`fitDialog` 只对 `QBoxLayout`/`QFormLayout` 根布局生效（`QFileDialog` 这类
  系统对话框不在范围内，SSTV 选图仍走 Qt 自带文件框）；SSTV 选图与真实收发、APRS 地图
  之外的联网功能仍未在手机上跑过。

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

- [x] **D2 HamLog 在 Android 上必然「不可用」，要的是如实降级** —— 降级半条已过，
  「换路径策略」那半条 11-01 由 C9 做掉了（`373d2a2`：Android 自建本地库），故这一页现在
  是真能用的第五个页签，判据随之改写为：**建库成功**时记 QSO 落盘（17:46 真机已过，见 C9）；
  **建库失败**时仍然整页不出、不摆死页面（`MainWindow` 构造里失败只 `qWarning`，
  `buildUi()` 照旧按 `hamlog_.available()` 决定这一页）。
  原代码事实（`logbook.cpp` 读 `APPDATA`，Android 上恒空）**已改**：`defaultDbPath()`
  在 `Q_OS_ANDROID` 下走 `radio::ui::settingsDir()`。
  判定：HamLog 相关按钮**不崩**、给出真实提示、不出现「写了 QSO 但其实没落盘」的假成功。

- [x] **D3 APRS 库落在哪**（写侧已验；「收下站点 → 重启 → 轨迹仍在」的读侧仍空，见下）
  代码事实：路径解析顺序 `RADIO_SUITE_APRS_DB` → `APPDATA` → `QStandardPaths::AppDataLocation`
  （[apps/desktop/aprs/persist.cpp](../apps/desktop/aprs/persist.cpp)）。
  判定：收下若干 APRS 站点 → force-stop → 重启，历史轨迹仍在，且能在 `run-as` 列出的
  私有目录里看到那个 `.db` 文件（大小非 0）。

  **2026-10-31 真机（第二包，含 APK 驱动修复 `065ff83`）结论：**
  - **修掉的根因**：原来 APK 里有 `libQt6Sql.so` 却**没有 sqldrivers 插件**
    （`unzip -l` 数过），于是 `QSqlDatabase::isDriverAvailable("QSQLITE")` 恒假，
    面板只会给「APRS 保存失败：Qt SQLite 驱动不可用」那句降级提示——不是路径问题，是打包问题。
  - **写侧已验**：面板开着过 30 s 存盘周期，应用**自己**建出
    `/data/user/0/org.radiosuite.app/files/aprs.db`，`su` 读出 36864 字节，
    头 16 字节是 `SQLite format 3\0`，里面确有 `stations`/`track`/`outbox`/`inbox` 四张表
    （`CREATE TABLE` 全在）。设备侧 `md5sum` = `e68beb7db007ed9e3126315858e137d4`；
    本地那份经 `adb shell su -c cat` 取回时多了 4 字节（**adb shell 会做 CRLF 转换**），
    把 `\r\n` 还原成 `\n` 后长度与 md5 与设备逐字节一致。
  - **读侧没验**：库里 0 条站点，「有数据 → 重启 → 数据还在」这条还没做到。
    卡在 C1：面板顶行「连接 APRS-IS」「RF 设置」「发信标」在竖屏下 bounds 全是
    `[0,0][0,0]`（顶行总宽超出屏宽被裁掉），手机上点不到，也就收不到站点。
    **别把这条当通过**——桌面 `[aprs_persist]` 用例覆盖的是同一份 load/save 代码，
    不等于手机上真机跑过。

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
