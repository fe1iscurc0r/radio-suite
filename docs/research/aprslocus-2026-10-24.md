# APRSLocus v2.1.34 调研（九维度）— 对 radio-suite（C++/Qt）的可用性

- 调研日期：2026-10-24 · 调研人：WO-268 施工（radio-suite M0）
- 对象：`dariondong/APRSLocus` `pubspec.yaml` 标 `version: 2.1.34+20134`，Dart/Flutter 3（sdk ^3.13.1），**GPL-3.0**（LICENSE 头部 GNU GPL v3, 2007）
- 规模实测：全仓 5,641 文件；`lib/**/*.dart` = **223** 个；`lib/state.dart` 单文件 **11,709 行**；`assets/aprs_syms/` = **3,571** 张符号 PNG（工单口径「37 表 3571 图标」命中）；另有根目录 `APRSicon/` 573 张 PNG（作者自绘 UI 图标，与符号表是两回事）
- 调研方式：shallow clone 到本地逐文件取证；每条结论附文件路径（行号为当时 tag 快照）

> 结论先行：**设计与数据可借，代码不可直接搬**。语言（Dart）与框架（Flutter Widget 树）与本项目
> （C++20/Qt Widgets）完全不重叠，AGPL §13 给了并通道但我们默认零拷贝；唯一值得逐行对照的是
> `lib/net/icom_lan_protocol.dart`——它与 `vendor/rsba1-core`（MIT，本项目协议基准）是**同一 Icom
> WLAN 协议的独立实现**，可交叉验证协议表。

---

## 1. 信息架构：2.0 的「沉浸地图 + 胶囊导航」

- 主壳 `lib/shell2.dart`：`class HomeShell2 extends StatefulWidget`（:59）。注释原文（:47）：
  「半透明 + 模糊 + 胶囊外形，选中用**滑动的指示胶囊**而不是 5 块色底」——解决的问题是传统
  BottomNavigationBar 五块色底把沉浸地图切成两层、视觉重心从地图挪到了导航条。
- 页面缓存：`IndexedStack`（:802-808 注释明确「切页不销毁（滚动位置、会话都保留）」，并批评
  「动画的每一帧都新建 IndexedStack 与四个页面」的反模式）。四个主页面：`home_page.dart` /
  `map_page.dart` / `stations_page.dart` / 设置族（`settings_pages.dart`，import 于 shell2.dart:14）。
- 胶囊高度不写死：:132-139 注释「高度由内容决定（状态胶囊的文字长度随语言变）」，首帧给估值，
  实测后校正；键盘弹出时用 `_navSpace` 同一口径避免胶囊「半截压在键盘上」（:658）。
- **对 C++ 的启示**：行为可借——对应 Qt 的 `QStackedWidget`（默认保活）+ 自绘胶囊控件
  （`QWidget` + `QPainterPath` 圆角 + 滑动指示器动画）。radio-suite 的日志/频谱/连接三区布局
  若走沉浸式，借鉴这套「指示器滑动、容器半透明模糊」的交互参数即可，无需任何代码。

## 2. 状态管理：11,709 行的 AppState 上帝对象（反面教材）

- `lib/state.dart:131` `class AppState extends ChangeNotifier`——单类聚合：连接、信标
  （`SmartBeaconTier` :72）、消息、成就（achievements）、`ConnStatus`（:11639）、自动上报
  （`AutoReportResult` :11527）、i18n 构造（:11229 注释承认「状态层没有 BuildContext，故直接
  按语言构造」的别扭）。持久化走 `shared_preferences`（import :10）。
- 没有 Provider/Riverpod/Bloc，就是手写 ChangeNotifier + 手动 `notifyListeners`。
- **对 C++ 的启示**：**不要学**。radio-suite 的 `core/RadioStateStore`（state.h）刻意做小：
  纯数据快照 + 订阅回调，按域拆（连接/频谱/日志各自独立 store）。APRSLocus 的教训是状态层
  一旦开始聚合 i18n、成就系统这类 UI 邻接域，就会长成万行巨石，测试无从下手。

## 3. 多源连接：TncTransport 抽象 + 五源并存

- 设备模型 `lib/net/tnc_base.dart:15` `class TncDevice`，`kind ∈ {'bluetooth','serial','usb'}`
  （:22），Android 蓝牙 SPP/RFCOMM 用 MAC、USB-OTG 用 `vid:pid:serial`（:12-13），串口线速
  仅 usb/serial 有意义（:28）；抽象层 `abstract class TncTransport`（tnc_base.dart:108）。
- KISS 编解码独立成 `lib/kiss.dart`（:4 注释：把帧转成 KISS-over-serial/bluetooth 字节流发到
  TNC，反向收包）——协议编解码与链路介质解耦，同一 KISS 层跑 TCP/串口/蓝牙三介质。
- 五类源全景：TNC（tnc_*.dart）＋声卡 AFSK（`afsk.dart`，编解码在 `lib/afsk.dart`）＋Icom
  WLAN（`net/icom_lan_*.dart`，27 文件）＋APRS-IS/HTTP 网关（`net/http_send*.dart` +
  pubspec `web_socket_channel: ^3.0.1`）＋音频文件回放（`net/audio_file_*.dart`）。
- **多收单发**：发射路径收敛于 KISS/TX 队列单一出口（kiss.dart:4 的单向描述），接收多源并存
  由 state 层调度——但**没有独立的 SourceManager 抽象**，连接编排内联在 state.dart 里。
- **对 C++ 的启示**：`TncDevice{kind,id,baud}` 三元组与 radio-suite 的 `ICivTransport` 接口
  同构，可直接借「**一个协议编解码层 + 多个介质 transport**」的切分；多收单发要做得比它好：
  落成 M1 的显式 `SourceManager`（RX 多源订阅、TX 单源路由），而不是塞进状态巨石。

## 4. Icom WLAN（CI-V over UDP）：与 rsba1-core 同源协议的独立实现

- `lib/net/icom_lan_protocol.dart`（1,190 行）：`loginRequestPacketSize = 0x80`（:497、:617
  「登录请求（0x80）」）——与 `vendor/rsba1-core` 的 ConnectServer 0x80B 登录包（其
  command_client.py 文档：「abs+0x40..0x4F passcode(username) 16B; +0x50..0x5F password」）
  是同一协议族（均源自 kappanhang 的逆向）。发现应答里 `civPort`/`audioPort` 从 0x42/0x46
  偏移大端读出（:736-737）。
- 周边家族（lib/net/ 共 27 个 icom_lan_*）：`_udp_channel.dart`、`_pcm16.dart`（音频编解码）、
  `_ptt_state_machine.dart`（**PTT 也有状态机**，可对照本项目 SafetyGate）、`_rx_session*.dart`
  /`_tx_audio_packetizer.dart`（收发音频会话）、`_audio_reorder_buffer.dart` +
  `_tracked_packet_store.dart`（**乱序重排与重传跟踪**——本项目 M1 UDP transport 的必修课）、
  `_reconnect_scheduler.dart` / `_connection_info_retry_policy.dart`（重连退避，对照工单
  「退避 2→4→8→16→30s」）、`_packet_inspector.dart`（抓包检视）。
- Windows 音频后端独立：`net/audio_winmm.dart`（WinMM 直采）。
- **对 C++ 的启示**：不搬代码，但 M1 写 UDP transport 前应把 `icom_lan_protocol.dart` 与
  `vendor/rsba1-core` 并排读一遍，把「登录 → 发现（civ/audio 端口）→ 会话 → 重传/重排」
  状态表整理进 docs，两份独立实现相互印证的地方就是协议的确定部分。

## 5. 地图与坐标：三 datum 自动适配 + 图源层纠偏

- 引擎：`flutter_map: ^8.3.1` + `vector_map_tiles: ^9.0.0-beta.11`（矢量瓦片，pubspec）。
- `lib/map_math.dart`：`enum MapDatum { wgs84, gcj02, bd09 }`（:41），按图源类型自动选 datum
  （:45-49「高德/腾讯 = GCJ-02；百度 = BD-09；矢量/国际图源 = WGS-84」）；BD 转换是两跳
  （:83「输入坐标是 BD-09（这里直接吃 WGS-84，内部转 GCJ→BD-09）」）；GCJ 纠偏放在**图源
  坐标系层面**处理而非全局改坐标（:69「国内图源的 GCJ-02 纠偏由调用方在图源坐标系层面
  处理（见 [_FallbackPainter]）」）。
- 离线瓦片：未见打包下载器（无 offline tiles 目录），工单「不做离线瓦片下载器」与它口径一致。
- **对 C++ 的启示**：坐标公式（WGS↔GCJ↔BD 三互转约百行）可**重写**（数学事实，无版权问题）；
  「按图源 datum 适配」的设计在 radio-suite 引入地图（P3+）时直接照搬。Qt 侧对应
  QGeoTIFF/自制瓦片层，暂不在本仓里程碑内。

## 6. 符号与设备识别：3,571 图标 + tocalls 离线库（数据许可注意）

- 符号表：`assets/aprs_syms/` 实测 **3,571 个 PNG**，文件名即符号代码（`2f21.png`…）；
  UI 图标另在 `theme_icons.dart`（由 `tool/gen_theme_icons.py` 生成，const Map 字面量）。
- tocalls：`assets/aprs_device/tocalls.json`，结构为带元数据的 dict：
  `schema:1` / `source: https://github.com/aprsorg/aprs-deviceid/blob/main/tocalls.yaml` /
  **`license: CC BY-SA 2.0`**。消费方在 `aprs_device.dart` / `models.dart` / `services.dart`。
- **对 C++ 的启示**：tocalls 是**数据资产且自带独立许可（CC BY-SA 2.0）**——若 radio-suite
  将来引入设备识别，须单独署名+同享该数据文件，与代码的 GPL/AGPL 分账处理（工单 §二已预警
  「tocalls 设备库与符号图标另有其自身许可，要单独核」，此为实证）。符号 PNG 的原始出处与
  许可它在仓库内未标注，二次使用前需向 APRS 符号委员会源头追溯。

## 7. 主题系统：六层职责切分（设计可整体借）

- `theme.dart`：材质层——「磨砂玻璃 / 云母」并明确语义边界：「这是**全局的显示偏好**（像深色
  模式、界面缩放一样），不是主题的一部分」。
- `theme_model.dart`：纯数据模型，「不依赖 Widget / 平台」，可单测。
- `theme_store.dart`：持久化；`theme_page.dart`：设置 UI；`theme_text.dart`：可被主题覆写的
  **文案取值口**（为什么不直接改 `S.of(context).map` 的理由写在文件头）；`theme_icons.dart`：
  生成产物（gen_theme_icons.py，勿手改）。
- **对 C++ 的启示**：分层思路（数据模型 / 持久化 / 材质偏好 / 文案口 / 生成图标映射）在 Qt 里
  对应 `QStyle`+`QSS`+自定义 `QPalette` 管理类，M3 做界面美化时按这五层拆文件，避免 theme
  长成单文件大杂烩。

## 8. 日志与 ADIF：导出完整、导入轻、QSO 触发点未完全定位

- `lib/adif.dart`（213 行）：导出方向——文件头 `_field('ADIF_VER')`/`PROGRAMID`/
  `PROGRAMVERSION`/`CREATED_TIMESTAMP` + `<EOH>`（:145-154），记录字段 `CALL`（含
  `stripSsid` 选项，:168）、`QSO_DATE` 等；中文相关处理在 APRS 消息层（pubspec
  `gbk_codec: ^0.4.0` 用于 GBK 消息），ADIF 本身按标准 ASCII/UTF-8。
- QSO 写入时机：未 grep 到独立 `addQso`/`insertQso` 入口（state.dart/services.dart 均无命中），
  推测内联在消息确认流程里——**此项留待二次调研**，radio-suite M4 按「ADIF 3.x 子集 + 何时
  写 QSO 由 UI 显式动作触发」自行设计即可，不依赖此结论。
- **对 C++ 的启示**：字段表（ADIF_VER/PROGRAMID/CALL/QSO_DATE/TIME_ON/...）是标准（ADIF
  spec），照 spec 实现即可；`stripSsid` 这种小开关值得抄作业。

## 9. 可复用度总表（C++/Qt 视角）

| 维度 | 判定 | 说明 |
|---|---|---|
| 1 信息架构 | **设计直接借** | 胶囊导航交互参数、IndexedStack=QStackedWidget 保活策略 |
| 2 状态管理 | **反面板子** | 万行 ChangeNotifier 是教训；radio-suite 按域拆小 store |
| 3 多源连接 | **设计直接借** | 「编解码层与介质解耦」= 本项目 civ + transport 切分；多收单发要做成显式 SourceManager（它没做好） |
| 4 Icom WLAN | **对照参考（重点）** | 与 rsba1-core 互为独立实现，M1 前并排读协议表；重排/退避文件是 checklist |
| 5 地图坐标 | **重写公式** | WGS/GCJ/BD 互转百行数学，无版权障碍；datum 按图源适配的设计照搬 |
| 6 符号/tocalls | **数据可借但单独核许可** | tocalls.json = CC BY-SA 2.0（实证）；3,571 符号 PNG 许可未标注需溯源 |
| 7 主题系统 | **设计直接借** | 五层拆分（模型/存储/材质/文案/生成图标） |
| 8 ADIF | **按标准实现** | 字段表照 ADIF spec，抄 stripSsid 小设计 |
| 9 总体 | **零代码拷贝** | 语言+框架完全不同；唯一「接近代码级」的价值是 icom_lan 协议表对照 |

### 数据类资产清单（许可分账）

| 资产 | 位置 | 许可 | radio-suite 处置 |
|---|---|---|---|
| tocalls 设备库 | `assets/aprs_device/tocalls.json` | **CC BY-SA 2.0**（文件内自证） | 引入时单独署名，不与 AGPL 混同 |
| APRS 符号 PNG ×3,571 | `assets/aprs_syms/` | 仓内未标注 | 暂不引入；要用先溯源（APRS 符号委员会） |
| UI 图标 ×573 | `APRSicon/` | 随仓 GPL-3.0 | 不引入（Qt 自绘） |
| 代码 | `lib/**` | GPL-3.0 → 依 AGPL §13 可并入 | **默认不并入**；如未来搬某模块，PR 须列源文件+commit+版权人（工单 §二规约） |

*（本文件为 radio-suite WO-268 M0 交付物之一；调研对象 GPL-3.0，本文档仅含事实描述与设计启示，未复制其代码。）*
