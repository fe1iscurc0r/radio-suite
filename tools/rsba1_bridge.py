#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""rsba1_bridge — radio-suite ↔ rsba1-core 真机桥（stdio JSON 行协议）.

rsba1-core（vendor/，MIT）是 IC-705 真机验证过的链路（2026-08-18 E2E）。
本桥把它变成 radio-suite 可用的 transport：C++ 侧发 {"cmd":"civ","hex":...}，
本进程经 RadioLink.send_civ/read_civ 完成收发并回 {"ok":true,"hex":...}。

用法: python rsba1_bridge.py <host> <user> <pass>   （stdin/stdout 各一行一个 JSON）
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# rsba1-core（MIT）在本仓为活子项目：libs/protocol/rsba1-core（subtree 同步自
# fe1iscurc0r/rsba1-core 上游，合作开发在上游 MIT 仓进行）
sys.path.insert(0, str(ROOT / "libs" / "protocol" / "rsba1-core" / "src"))

from rsba1.radio_link import RadioLink, RadioLinkError  # noqa: E402


def emit(obj: dict) -> None:
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def main() -> int:
    if len(sys.argv) != 4:
        emit({"ok": False, "error": "usage: rsba1_bridge.py <host> <user> <pass>"})
        return 2
    host, user, pwd = sys.argv[1], sys.argv[2], sys.argv[3]
    link: RadioLink | None = None
    next_id = 0

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError as e:
            emit({"ok": False, "error": f"bad json: {e}"})
            continue
        rid = req.get("id", next_id)
        next_id = max(next_id, rid + 1)
        cmd = req.get("cmd", "")

        try:
            if cmd == "open":
                if link is not None:
                    emit({"id": rid, "ok": True, "note": "already open"})
                    continue
                link = RadioLink(host, user, pwd)
                link.open()
                emit({"id": rid, "ok": True})
            elif cmd == "civ":
                if link is None:
                    emit({"id": rid, "ok": False, "error": "not open"})
                    continue
                frame = bytes.fromhex(req.get("hex", ""))
                # CI-V 帧: FE FE to from cmd [sub..] FD —— read_civ 按 cmd(+sub) 等应答
                c = frame[4]
                sub = bytes(frame[5:7]) if len(frame) > 6 else b""
                link.send_civ(frame)
                resp = link.read_civ(c, timeout=float(req.get("timeout", 2.0)), sub=sub)
                emit({"id": rid, "ok": True, "hex": resp.hex()})
            elif cmd == "close":
                if link is not None:
                    link.close()
                    link = None
                emit({"id": rid, "ok": True})
                return 0
            else:
                emit({"id": rid, "ok": False, "error": f"unknown cmd: {cmd}"})
        except RadioLinkError as e:
            emit({"id": rid, "ok": False, "error": f"radiolink: {e}"})
        except (ValueError, OSError) as e:
            emit({"id": rid, "ok": False, "error": str(e)})
    if link is not None:
        link.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
