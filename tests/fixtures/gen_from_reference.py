#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""gen_from_reference.py — 从 vendor/rsba1-core（MIT）生成 CI-V fixtures。

用途：保证 C++ 实现与 Python 参考实现（IC-705 真机验证 2026-08-18）字节级一致。
运行：python tests/fixtures/gen_from_reference.py   （在仓库根执行亦可）
产物：同目录 *.hex，每行一帧（小写十六进制、无分隔符）。
"""
from __future__ import annotations

import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "vendor" / "rsba1-core" / "src"))

from rsba1.ctypes_wrappers import civ_commands as civcmd  # noqa: E402

CTRL = 0xE0   # 经典控制器地址（工单帧示例）
RADIO = 0xA4  # IC-705


def resp(cmd: int, payload: bytes, to: int = 0x00, frm: int = RADIO) -> bytes:
    """电台应答帧：FE FE <to=控制器> <frm=电台> <cmd> <payload> FD。"""
    return bytes([0xFE, 0xFE, to, frm, cmd]) + payload + bytes([0xFD])


def main() -> int:
    # —— 先自证参考实现行为未漂移（与 civ.cpp 的硬编码期望互为锁）——
    assert civcmd.build_frame(RADIO, CTRL, bytes([0x03])).hex() == "fefea4e003fd"
    assert civcmd.freq_to_bytes(14_270_000).hex() == "0000271400"
    assert civcmd.freq_to_bytes(145_000_000).hex() == "0000004501"
    assert civcmd.bytes_to_freq(bytes.fromhex("0000271400")) == 14_270_000

    frames = {
        "read_freq_cmd.hex": civcmd.build_frame(RADIO, CTRL, civcmd.read_freq_bytes()),
        "set_freq_cmd_145m.hex": civcmd.build_frame(RADIO, CTRL, civcmd.set_freq_bytes(145_000_000)),
        "ptt_on_cmd.hex": civcmd.build_frame(RADIO, CTRL, civcmd.ptt_on_bytes()),
        "read_freq_resp_14270k.hex": resp(0x03, civcmd.freq_to_bytes(14_270_000)),
        "read_mode_resp_fm.hex": resp(0x04, bytes([0x05, 0x01])),      # FM / 滤波器1
        "read_smeter_resp_s9.hex": resp(0x1A, bytes([0x03, 120])),     # S9
        "read_ptt_resp_on.hex": resp(0x14, bytes([0x0C, 0x01])),
        "read_ptt_resp_off.hex": resp(0x14, bytes([0x0C, 0x00])),
    }

    header = (
        "# 由 gen_from_reference.py 生成，勿手改\n"
        "# 源: vendor/rsba1-core (MIT) · IC-705 真机验证 2026-08-18\n"
    )
    for name, frame in frames.items():
        (HERE / name).write_text(header + frame.hex() + "\n", encoding="utf-8")
        print(f"{name}: {frame.hex()}")
    print("OK — 8 fixtures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
