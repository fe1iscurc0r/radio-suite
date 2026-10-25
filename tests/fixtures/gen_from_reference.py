#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""gen_from_reference.py — 从 libs/protocol/rsba1-core（MIT）生成 CI-V fixtures。

用途：保证 C++ 实现与 Python 参考实现（IC-705 真机验证 2026-08-18）字节级一致。
运行：python tests/fixtures/gen_from_reference.py   （在仓库根执行亦可）
产物：同目录 *.hex，每行一帧（小写十六进制、无分隔符）。
"""
from __future__ import annotations

import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "libs" / "protocol" / "rsba1-core" / "src"))

from rsba1.ctypes_wrappers import civ_commands as civcmd  # noqa: E402
from rsba1.serial import command_client as cc  # noqa: E402
from rsba1.serial.serial_codec import build_serial_frame  # noqa: E402

CTRL = 0xE0   # 经典控制器地址（工单帧示例）
RADIO = 0xA4  # IC-705

HEADER = (
    "# 由 gen_from_reference.py 生成，勿手改\n"
    "# 源: libs/protocol/rsba1-core (MIT) · IC-705 真机验证 2026-08-18\n"
)


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

    # ---- UDP (RS-BA1 V2) 协议 fixtures：固定参数，与 C++ rsba1_proto 逐字节互锁 ----
    LOCAL_SID, REMOTE_SID = 0x2A94BC02, 0x19F8B4F7
    AUTH_ID = bytes.fromhex("112233445566")
    A8_ID = bytes(range(0xA0, 0xB0))  # 16B

    def _synth(first_byte: int, fields: dict, int_fields: dict) -> bytes:
        """按偏移合成定长应答包（长度精确，杜绝手拼 pad 算错）。"""
        import struct as _s
        size = {0x60: 0x60, 0x90: 0x90}[first_byte]
        buf = bytearray(size)
        buf[0] = first_byte
        if first_byte == 0x60:
            buf[6:8] = b"\x01\x00"
        for off, val in fields.items():
            b = val if isinstance(val, bytes) else _s.pack(">i", val)
            buf[off:off + len(b)] = b
        for off, val in int_fields.items():
            buf[off:off + 4] = _s.pack(">i", val)
        return bytes(buf)

    udp = {
        "udp_pkt3.hex": cc.build_pkt3(LOCAL_SID),
        "udp_pkt6.hex": cc.build_pkt6(LOCAL_SID, REMOTE_SID),
        "udp_disconnect.hex": cc.build_disconnect_pkt(LOCAL_SID, REMOTE_SID),
        "udp_login.hex": cc.build_login_request(
            "linnan", "test123", local_sid=LOCAL_SID, remote_sid=REMOTE_SID,
            outer_seq=1, inner_seq=0, auth_start_id=b"\xab\xcd"),
        "udp_auth02.hex": cc.build_auth_request(
            0x02, local_sid=LOCAL_SID, remote_sid=REMOTE_SID,
            outer_seq=2, inner_seq=0x12, auth_id=AUTH_ID),
        "udp_auth05.hex": cc.build_auth_request(
            0x05, local_sid=LOCAL_SID, remote_sid=REMOTE_SID,
            outer_seq=3, inner_seq=0x15, auth_id=AUTH_ID),
        "udp_connecttrans.hex": cc.build_connect_trans_request(
            "linnan", local_sid=LOCAL_SID, remote_sid=REMOTE_SID,
            outer_seq=4, inner_seq=0x30, auth_id=AUTH_ID, a8_reply_id=A8_ID),
        # 应答样例（按解析器规格合成；起手特征/偏移与真机抓包一致）
        "udp_login_resp_ok.hex": _synth(0x60, {26: AUTH_ID}, {48: 0}),
        "udp_login_resp_badpass.hex": _synth(0x60, {}, {48: -2}),
        "udp_connecttrans_resp.hex": _synth(
            0x90,
            {8: (0x11111111).to_bytes(4, "big"),
             12: (0x22222222).to_bytes(4, "big"),
             26: AUTH_ID,
             64: b"IC-705\x00",
             96: b"\x01"},
            {}),
        # Serial 帧：CI-V 读频率 (sseq=7, bulk) 与 open/close 单字节控制 (sseq=0)
        "udp_serial_frame_civ.hex":
            build_serial_frame(civcmd.build_frame(RADIO, CTRL, civcmd.read_freq_bytes()),
                               sseq=7, bulk=True),
        "udp_serial_frame_open.hex": build_serial_frame(b"\x05", sseq=0, bulk=False),
    }
    frames.update(udp)

    # passcode 互锁样例
    (HERE / "udp_passcode_linnan.hex").write_text(
        HEADER + cc.passcode("linnan").hex() + "\n", encoding="utf-8")
    (HERE / "udp_passcode_a.hex").write_text(
        HEADER + cc.passcode("a").hex() + "\n", encoding="utf-8")

    for name, frame in frames.items():
        (HERE / name).write_text(HEADER + frame.hex() + "\n", encoding="utf-8")
        print(f"{name}: {frame.hex()}")
    print(f"OK — {len(frames) + 2} fixtures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
