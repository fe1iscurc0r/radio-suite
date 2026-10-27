# SPDX-License-Identifier: AGPL-3.0-or-later

## apps/desktop/audio

RtAudio（MIT）本地音频设备层：

- `AudioIn`：采集回调（Voice Memory 录音、50003 麦克风上行）；
- `AudioOut`：一次性播放（SSTV / Voice Memory）；
- `AudioSink`：**流式播放**（环形队列、有界丢最旧、欠载补零），用于 50003 RX；
- `audio-in-smoke`：采集冒烟程序。

设备一律用系统默认输入/输出（`defaultInputDevice()` / `defaultOutputDevice()`）；
WASAPI 设备号非连续，不能假设 0。暂无设备选择 UI。

50003 编解码在 `core/audio_stream.*`，收发接线在 `core/transport_udp.*`
（`IAudioLink`），app 侧编排见 `app/network_audio.*` + `app/audio_bridge.h`
（半双工门禁）。**真机通话未验**，复验清单见
`docs/original-rsba1-feature-map.md` §L.6。
