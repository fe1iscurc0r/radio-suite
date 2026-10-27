# SPDX-License-Identifier: AGPL-3.0-or-later

## apps/desktop/audio

RtAudio（MIT）本地音频输入/输出：`AudioIn` 用于 Voice Memory 录音，`AudioOut`
用于本机播放及 SSTV/Voice Memory 发射音频；另有 `audio-in-smoke` 采集冒烟程序。

这里不是 RS-BA1 UDP 50003 网络音频接入层。`core/audio_stream.*` 与
`transport_udp.*` 虽有音频包编解码/收发能力，但尚未接入 app 的 AudioIn/AudioOut。
主窗口与 HTTP 总线当前都使用合成谱，不把麦克风或 50003 当作频谱源。
