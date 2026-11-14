#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""smoke_stdio — 用「宿主真的会用的方式」跑一遍适配器：子进程 + stdio + 真总线.

单测打的是 dispatch 纯函数；这条验的是另一半：进程起得来吗、帧框得对吗、
真总线返的字段我们照不照得出来。所以要手动配一个在跑的总线，CI 里没有 Qt 总线，
这条不进 CI（CI 跑 tests/ 那两条纯标准库用例）。

    radio-suite --headless --bus 8790 &
    python tools/mcp_radio/smoke_stdio.py --url http://127.0.0.1:8790
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from mcp_radio.server import force_utf8_stdio  # noqa: E402


def frames(proc):
    """一行一帧：读 stdout 直到拿到带 id 的应答（日志都在 stderr，不会混进来）。"""
    while True:
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError("适配器进程退出了，stderr 里看原因")
        line = line.strip()
        if line:
            return json.loads(line)


def main() -> int:
    force_utf8_stdio()
    ap = argparse.ArgumentParser(prog="smoke_stdio")
    ap.add_argument("--url", default=os.environ.get("RADIO_BUS_URL", "http://127.0.0.1:8765"))
    args = ap.parse_args()

    env = dict(os.environ, PYTHONPATH=str(HERE.parent), RADIO_BUS_URL=args.url)
    proc = subprocess.Popen([sys.executable, "-m", "mcp_radio", "--url", args.url],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, encoding="utf-8", env=env)
    failures: list[str] = []

    def send(msg):
        proc.stdin.write(json.dumps(msg, ensure_ascii=False) + "\n")
        proc.stdin.flush()

    def check(label, cond, detail=""):
        print(("  PASS " if cond else "  FAIL ") + label + ("" if cond else f" :: {detail}"))
        if not cond:
            failures.append(label)

    try:
        send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
              "params": {"protocolVersion": "2026-07-28", "capabilities": {},
                         "clientInfo": {"name": "smoke", "version": "0"}}})
        init = frames(proc)
        check("initialize 回了协议版本", init.get("result", {}).get("protocolVersion") == "2026-07-28", init)
        send({"jsonrpc": "2.0", "method": "notifications/initialized"})

        send({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        tools = frames(proc)["result"]["tools"]
        names = [t["name"] for t in tools]
        check("tools/list 三件只读", names == ["get_state", "get_spectrum", "link_health"], names)

        send({"jsonrpc": "2.0", "id": 3, "method": "tools/call",
              "params": {"name": "get_state", "arguments": {}}})
        st = frames(proc)["result"]
        payload = st.get("structuredContent") or {}
        check("get_state 没报错", not st.get("isError"), st.get("content"))
        for field in ("freq_hz", "mode", "ptt_on", "smeter_dbm", "freq_mhz", "state_age_ms"):
            check(f"get_state 带 {field}", field in payload, sorted(payload))

        send({"jsonrpc": "2.0", "id": 4, "method": "tools/call",
              "params": {"name": "get_spectrum", "arguments": {"max_points": 8}}})
        sp = frames(proc)["result"]
        spec = sp.get("structuredContent") or {}
        check("get_spectrum 没报错", not sp.get("isError"), sp.get("content"))
        check("频谱压到 8 点", len(spec.get("power_db_peaks", [])) == 8, spec.get("power_db_peaks"))
        check("横轴声明是音频带", spec.get("axis", {}).get("kind") == "audio_baseband_hz", spec.get("axis"))
        check("峰位换算 = bin_hz × bin（peak_hz 只保留一位小数）",
              abs(spec.get("peak_hz", -1) - spec.get("bin_hz", 0) * spec.get("peak_bin", -1)) <= 0.05,
              spec)

        send({"jsonrpc": "2.0", "id": 5, "method": "tools/call",
              "params": {"name": "link_health", "arguments": {"window_ms": 200}}})
        lh = frames(proc)["result"]
        health = lh.get("structuredContent") or {}
        check("link_health 没报错", not lh.get("isError"), lh.get("content"))
        check("总线可达", health.get("bus_reachable") is True, health)
        check("帧号在推进", health.get("spectrum_advancing") is True, health)

        send({"jsonrpc": "2.0", "id": 6, "method": "tools/call",
              "params": {"name": "ptt", "arguments": {}}})
        bad = frames(proc)
        check("发射类工具还不存在（M1 只读）",
              bad.get("error", {}).get("code") == -32602, bad)

        send({"jsonrpc": "2.0", "id": 7, "method": "prompts/list"})
        check("不支持的方法给 -32601",
              frames(proc).get("error", {}).get("code") == -32601)
    finally:
        proc.stdin.close()
        err = proc.stderr.read()
        proc.wait(timeout=10)

    print("\n--- 适配器 stderr（日志只许出现在这里）---")
    print(err.strip() or "(空)")
    print(f"\n结论：{len(failures)} 项失败" + ("" if not failures else " -> " + ", ".join(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
