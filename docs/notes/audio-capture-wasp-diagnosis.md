# WASAPI 麦克风采集：样本预算失真诊断（2026-10-25）

## 现象

本机（Realtek 麦克风阵列，RtAudio device id=130）采集 2 秒：

```
captured 2s -> 60 frames (28.5 fps, 期望 50) peak=548
sample budget: 57600 / 100890 (57.1%)
```

即**拿到的样本只有理论预算的 ~57%**：20ms 帧（960 样本）按 28.5fps 送出，
语音虽可懂（峰 548 有信号），但**录音时长会系统性偏短 ~43%**——若按墙钟计
60s 上限，实际只能录到 ~34s 的内容（现已改为按样本数计上限，见「已落地的补救」）。

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
| 语音发射 / Voice Memory 录音 | 时长偏短 43% | **已缓解**：录音上限与时长显示都按实际样本数计（见下），不再被墙钟提前截断；**残余**：文件里的 48k 样本仍少于真实声学时长，回放会偏快/偏短，根治需独占模式 |
| SSTV 发送 | 无 | 发送走 **AudioOut 播放**链，不经过采集路径 |
| 通话（PTT 语音上行到电台） | 对方听到的话会短/变速 | 与上同因；真机 TX 走引擎侧 jitter buffer 时另有兜底 |

## 已落地的补救（2026-10-26 核对）

`apps/desktop/app/voice_memory.cpp`：

- 录音上限按样本数：`rec_pcm_.size() >= kRate * kMaxSeconds`（48 000 × 60），
  不再用墙钟计时——否则共享模式少交样本会让上限提前到 ~34s 墙钟触发。
- 时长显示按样本数折算：`rec_pcm_.size() / double(kRate)`，与文件内容一致。

因此「60s 上限」现在指 **60s 的 48k 样本**；若共享模式仍只交 ~57% 样本，
录满 60s 样本约需 ~105s 墙钟，且回放时长仍比真实声学事件短 ~43%。
彻底的时基修正仍取决于下面第 2/3 条（独占模式或 IAudioClient 直连），未做。


## 复现命令

```bash
cd build/apps/desktop/audio && audio-in-smoke.exe
# 打印：captured Xs -> N frames (fps) peak=... 与 sample budget 百分比
```

## 建议的下一步（任选）

1. ~~**按样本数计时**~~：**已落地**（`voice_memory.cpp` 上限与时长显示均按样本数）。
2. **WASAPI 独占模式**：RtAudio 编译期 `__WINDOWS_WASAPI_EXCLUSIVE__`（或
   `RtAudio::WASAPI` + `useExclusive`），绕过共享模式的引擎周期交付。
3. **换 PortAudio/WASAPI 直连**：用 Windows 的 `IAudioClient` 独占模式自己拿流，
   彻底可控（工程量大）。
