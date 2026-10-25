# WO-273 施工记录 · 无线电双端定位（竞品盘点 → 裁决）

## §0 授权说明（如实记录，不回避）

工单硬边界写"不动 radio-suite（268 RESERVED，未指定领取人前任何人不许开工）"。
事实：**2026-10-24 师傅当面指定本人领取 268**，radio-suite 已由本人施工至 M2
（main 分支 12 提交，255 文件，测试 49/49）。本单阶段一为纯文档、阶段三为"对齐
268 产物"，执行者=268 领取人本人，**授权链闭合**；阶段二（手机端薄片）仍按工单
等师傅点头后才动工。

## §1 开工前必读四项的实查结论（真实命令与发现）

1. **268 §零/§一**：双形态 + C++20/Qt6 选型表——本单 D 节结论"维持"，未改其任何选型段。
2. **rsba1-core 实读**（`vendor/rsba1-core`，与远端同源）：
   ```bash
   grep -nE "audio|Audio|50003" src/rsba1/radio_link.py   # 8 处命中：50003 信道握手维活
   grep -rn "codec|PCM|opus|ulaw" src/rsba1/              # 0 命中（无音频数据通路）
   grep -rn "0x27|spectrum" src/rsba1/                    # 0 命中（频谱一行没有）
   ```
   与工单预核判断一致；唯一修正点确认成立。
3. **rigplane**（WebFetch README，未 clone 全量——97MB<100MB 但按只读精神处理）：
   MIT / Python asyncio / Icom LAN+USB+Yaesu / Svelte5 webUI 四皮肤（scope+waterfall）/
   内置 rigctld / 3.0 公测（11 层包 5600+ 测试）/ 开源核心+闭源 pro 计划。
4. **wfweb**（WebFetch README，76MB 只读）：GPL-3.0 / headless wfview /
   浏览器 RX+麦克风 TX / FT8·JS8·CW·FreeDV·AX.25 全家 / ADIF+rigctld+REST /
   110★ 4155 commits 跟随上游。
   sdroxide/AetherSDR/Nexus：仅 README 级（硬边界禁 clone >100MB）。

## §2 阶段一交付物与验收输出

| 交付 | 路径 |
|---|---|
| A+B 竞品总表+六层矩阵 | `docs/research/radio-competitors-2026-10-25.md` |
| C+D+E+F 裁决 | `docs/research/wo273-verdicts.md` |

验收命令真实输出（2026-10-25 实跑）：
```
$ grep -c '^| `' docs/research/radio-competitors-2026-10-25.md
13            （≥13 ✓；另含矩阵表 9 行以 | ** 开头未计入）
$ grep -c "rigplane" docs/research/radio-competitors-2026-10-25.md
3             （≥2 ✓）
$ for p in rsba1-core kappanhang rigplane wfweb wfview sdroxide AetherSDR Nexus tx-5dr cp705 ic705controler 9GRadio qFT8; do grep -q "$p" ... ; done
OK ×13        （13 项全部出现 ✓）
```

## §3 裁决速览（细节见裁决文件）

- **Windows：维持** C++20+Qt6；三条竞品修正（传输后端抽象/Qt6 分层规矩/总线契约稳定）。
- **手机：主路 E1′ PWA**（`apps/web/` 消费本仓总线），**退路 E3** Kotlin 薄壳；
  换路门槛 = PTT 端到端 >200ms 或 WebAudio 压不进 100ms。
- **仓结构：单仓 monorepo**（apps/desktop + apps/web + libs/protocol[subtree rsba1-core，MIT 头保留] + libs/dsp）；阶段三执行平移。
- **唯一差异点**：agent 可调（MCP/总线）+ QSO 直落 HamLog + lumo 可嵌 + 双端薄壳；
  协议栈 MIT / 壳 AGPL 双许可链路干净。
- **时间窗**：rigplane 3.0 stable 后 1–2 版周期（≈2–3 个月）；PWA 借力不竞速。

## §4 阶段三实施记录（师傅 2026-10-25 "ok" 点头后开工）

| 批 | commit | 内容 | 验证 |
|---|---|---|---|
| ① | `70bedde` | kappanhang 音频通路移植（AudioTxPacker/AudioRxBuffer，MIT 标注+NOTICE 上游表） | 53/53 |
| ② | `8238339` | transport_udp 音频双通路接入 + RigctldServer（rigplane 语义+dump_state 移植，IC-705 化） | 53/53 + **nc 实测 f/F/m/chk_vfo 全通** |
| ③ 待办 | — | 0x27 频谱解帧（wfview 抓包参数）+ monorepo 平移 + PWA（阶段二） | — |

精读修正记录：kappanhang 音频**无 u-law**——48kHz s16le 直传，TX 20ms=1920B 拆
1364+556 两包；整合图谱 v1 的 u-law 说法待更正（下一版图谱更新）。

## §未完成项 / 移交（原 §4 上移合并）

- 阶段二（PWA 薄片）：等师傅点头；验收硬指标（ADIF 一条真记录 + 延迟数字文件 + K40 实拍）照工单执行。
- 阶段三（monorepo 平移 + desktop delta 清单）：等师傅点头；本人即 268 领取人，可直接动 apps/desktop。
- 竞品追加项（SDR++/SDRangel/Log4OM/JTDX）：骨架已留位，阶段二开工前补 API 实查。
