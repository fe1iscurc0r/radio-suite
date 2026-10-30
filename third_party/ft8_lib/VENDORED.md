# ft8_lib（vendored）

- 上游：https://github.com/kgoba/ft8_lib （MIT，Copyright (c) 2018 Kārlis Goba）
- 版本：`master` 分支快照，2026-10-30 拉取
  （tarball `https://github.com/kgoba/ft8_lib/archive/refs/heads/master.tar.gz`，
  sha256 `af4e9680e869b139507f7fb0356f4b9641cb1768151f61e7c7e24893c1b68bf8`）
- 收录范围：`ft8/`（编码/解码/CRC/LDPC/消息打包）、`common/monitor.c`
  （STFT 瀑布 + sync 搜索，即解调前端）、`fft/`（kiss-fft），以及上游 LICENSE/README。
- 未收录：`demo/`（含 ALSA/WASAPI 播放与 VLA 写法）、`ft4_ft8_public/`（Fortran 参考表）、
  `utils/`、`test/`。`common/audio.c`、`common/wave.c` 是 demo 专用，同样未收录。
- 本地修改（仅两处 MSVC 兼容，MSVC C 无 VLA）：
  1. `common/monitor.c` `monitor_process()`：`timedata/freqdata` 变长数组改为
     `malloc/free`（尺寸仍由 nfft 决定，OOM 时直接返回）。
  2. `ft8/decode.c` `ft8_decode_multi_symbols()`：`s2[n_tones]` 改为定长 `s2[512]`
     并加越界守卫（该函数当前无调用者）。
  其余字节保持上游原样。
- 本地新增（不改上游文件）：`common/compat_msvc.c` —— Windows CRT 无 POSIX
  `stpcpy`（`ft8/message.c` 有一处使用），该文件仅在 `_MSC_VER` 下提供实现。
- 构建：`third_party/CMakeLists.txt` 编成静态库 `radio_ft8lib`（C11，第三方告警关闭）。
