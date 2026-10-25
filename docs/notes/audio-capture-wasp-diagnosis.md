# WASAPI 麦克风采集：样本预算失真诊断（2026-10-25）

## 现象

本机（Realtek 麦克风阵列，RtAudio device id=130）采集 2 秒：

```
captured 2s -> 60 frames (28.5 fps, 期望 50) peak=548
sample budget: 57600 / 100890 (57.1%)
```

即**拿到的样本只有理论预算的 ~57%**：20ms 帧（960 样本）按 28.5fps 送出，
语音虽可懂（峰 548 有信号），但**录音时长会系统性偏短 ~43%**——Voice Memory
的 60s 上限实际只有 ~34s。

## 已排除的假设

1. **不是设备原生 24k 与请求 48k 不匹配**：实测 `preferredSampleRate` 与
   `getStreamSampleRate()` 均为 48000；按原生速率开流 + 回调内线性重采样到 48k
   的实验版本反而更差（预算 45.8%），已回滚。
2. **不是缓冲粒度问题**：`buffer_frames=480`（10ms）下同样失真。
3. **不是回调丢帧**：回调内样本数正常，缺口在 RtAudio 交付层。

## 结论（已定位到第三方层）

缺口在 **RtAudio 的 WASAPI 共享模式（`InitializeSharedAudioStream`）**：非引擎
原生速率的请求下，共享模式按引擎周期交付，样本量与墙钟不匹配（RtAudio 5.x
已知行为；其 `WASAPI` 后端未暴露 `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM` 开关，
无法让 Windows 做官方重采样）。

## 影响与处置

| 用途 | 影响 | 处置 |
|---|---|---|
| 语音发射 / Voice Memory 录音 | 时长偏短 43% | **已知问题**；补救：录音时记录实际样本数而非墙钟（按样本数计上限），或改用 WASAPI 独占模式（`WASAPI_EXCLUSIVE`，RtAudio 需编译期开关） |
| SSTV 发送 | 无 | 发送走 **AudioOut 播放**链，不经过采集路径 |
| 通话（PTT 语音上行到电台） | 对方听到的话会短/变速 | 与上同因；真机 TX 走引擎侧 jitter buffer 时另有兜底 |

## 复现命令

```bash
cd build/apps/desktop/audio && audio-in-smoke.exe
# 打印：captured Xs -> N frames (fps) peak=... 与 sample budget 百分比
```

## 建议的下一步（任选）

1. **按样本数计时**：录音上限改用 `rec_pcm_.size() >= 48000*60` 而非墙钟——
   一行修复，录音时长即正确（Wall-clock 估算的 60s 变 105s）。
2. **WASAPI 独占模式**：RtAudio 编译期 `__WINDOWS_WASAPI_EXCLUSIVE__`（或
   `RtAudio::WASAPI` + `useExclusive`），绕过共享模式的引擎周期交付。
3. **换 PortAudio/WASAPI 直连**：用 Windows 的 `IAudioClient` 独占模式自己拿流，
   彻底可控（工程量大）。
