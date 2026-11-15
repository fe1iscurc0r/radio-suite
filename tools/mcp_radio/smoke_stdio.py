#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""smoke_stdio — 用「宿主真的会用的方式」跑一遍适配器：子进程 + stdio + 真总线.

单测打的是 dispatch 纯函数；这条验的是另一半：进程起得来吗、帧框得对吗、
真总线返的字段我们照不照得出来。所以要手动配一个在跑的总线，CI 里没有 Qt 总线，
这条不进 CI（CI 跑 tests/ 那两条纯标准库用例）。

    radio-suite --headless --mock --bus 8790 &
    python tools/mcp_radio/smoke_stdio.py --url http://127.0.0.1:8790

默认只跑「读 + 被拒的路」：这些不改电台状态，对着真机也安全。
会真的按键发射、真的改频率/制式的那一段要显式 --allow-tx 才跑，跑完把频率和
制式还原成开跑前的样子。**别对着接了天线的真机加这个开关**，先用 --mock。
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from mcp_radio.server import force_utf8_stdio  # noqa: E402

EXPECTED_TOOLS = ["get_state", "get_spectrum", "link_health",
                  "tune", "set_mode", "ptt", "log_qso", "export_adif"]


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
    ap.add_argument("--allow-tx", action="store_true",
                    help="跑会改电台状态的那一段（设频/设制式/两击发射）。只对着 --mock 用")
    args = ap.parse_args()

    env = dict(os.environ, PYTHONPATH=str(HERE.parent), RADIO_BUS_URL=args.url)
    proc = subprocess.Popen([sys.executable, "-m", "mcp_radio", "--url", args.url],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, encoding="utf-8", env=env)
    failures: list[str] = []
    skipped: list[str] = []
    next_id = [0]

    def send(msg):
        proc.stdin.write(json.dumps(msg, ensure_ascii=False) + "\n")
        proc.stdin.flush()

    def call(name, arguments=None):
        next_id[0] += 1
        send({"jsonrpc": "2.0", "id": next_id[0], "method": "tools/call",
              "params": {"name": name, "arguments": arguments or {}}})
        return frames(proc)["result"]

    def body(result):
        return result.get("structuredContent") or {}

    def why(result):
        return " | ".join(c.get("text", "") for c in result.get("content", []))

    def check(label, cond, detail=""):
        print(("  PASS " if cond else "  FAIL ") + label + ("" if cond else f" :: {detail}"))
        if not cond:
            failures.append(label)

    def skip(label):
        print("  SKIP " + label + "（要 --allow-tx）")
        skipped.append(label)

    def wait_state(pred, timeout_s=2.0):
        """等回读追上。tune/ptt 返回只代表「命令已发」，store 要等下一轮 250 ms 轮询，
        所以紧接着读 state 会读到旧值 —— 那是总线的真实语义，不是适配器的 bug。"""
        deadline = time.monotonic() + timeout_s
        st = body(call("get_state"))
        while not pred(st) and time.monotonic() < deadline:
            time.sleep(0.05)
            st = body(call("get_state"))
        return st

    original = {}
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
        check("tools/list 八件（三读 + 五写）", names == EXPECTED_TOOLS, names)
        by_name = {t["name"]: t for t in tools}
        check("ptt 标了 destructiveHint 且不是只读",
              by_name["ptt"]["annotations"].get("destructiveHint") is True
              and by_name["ptt"]["annotations"].get("readOnlyHint") is False,
              by_name["ptt"]["annotations"])
        check("三个读工具仍标只读",
              all(by_name[n]["annotations"].get("readOnlyHint") is True
                  for n in ("get_state", "get_spectrum", "link_health", "export_adif")),
              {n: by_name[n]["annotations"] for n in ("get_state", "export_adif")})
        check("写工具都不许带 readOnlyHint",
              all(by_name[n]["annotations"].get("readOnlyHint") is False
                  for n in ("tune", "set_mode", "ptt", "log_qso")),
              {n: by_name[n]["annotations"] for n in ("tune", "log_qso")})

        st = call("get_state")
        payload = body(st)
        check("get_state 没报错", not st.get("isError"), why(st))
        for field in ("freq_hz", "mode", "ptt_on", "ptt_phase", "smeter_dbm",
                      "freq_mhz", "state_age_ms"):
            check(f"get_state 带 {field}", field in payload, sorted(payload))
        check("开局门禁在 rx", payload.get("ptt_phase") == "rx", payload.get("ptt_phase"))
        original = {"freq_hz": payload.get("freq_hz"), "mode": payload.get("mode")}

        sp = call("get_spectrum", {"max_points": 8})
        spec = body(sp)
        check("get_spectrum 没报错", not sp.get("isError"), why(sp))
        check("频谱压到 8 点", len(spec.get("power_db_peaks", [])) == 8, spec.get("power_db_peaks"))
        check("横轴声明是音频带", spec.get("axis", {}).get("kind") == "audio_baseband_hz", spec.get("axis"))
        check("峰位换算 = bin_hz × bin（peak_hz 只保留一位小数）",
              abs(spec.get("peak_hz", -1) - spec.get("bin_hz", 0) * spec.get("peak_bin", -1)) <= 0.05,
              spec)

        lh = call("link_health", {"window_ms": 200})
        health = body(lh)
        check("link_health 没报错", not lh.get("isError"), why(lh))
        check("总线可达", health.get("bus_reachable") is True, health)
        check("帧号在推进", health.get("spectrum_advancing") is True, health)
        check("帧率用实测间隔算（measured ≥ requested）",
              health.get("measured_window_ms", 0) >= health.get("requested_window_ms", 0),
              health)

        # ---- 被拒的路：这些不改电台状态，对着真机也安全 ----
        before = body(call("get_state")).get("freq_hz")
        bad_tune = call("tune", {"freq_hz": 20_000})  # 30 kHz 以下，接收范围外
        check("范围外 setfreq 回 isError", bad_tune.get("isError") is True, why(bad_tune))
        check("被拒后电台频率没动", body(call("get_state")).get("freq_hz") == before, before)
        check("freq_hz 传字符串被挡在参数校验",
              call("tune", {"freq_hz": "7100000"}).get("isError") is True)
        check("不认识的制式回 isError",
              call("set_mode", {"mode": "NOPE"}).get("isError") is True)

        # 这条是两击门禁的核心：未武装就带 confirm，必须一个字节的总线动作都不发
        premature = call("ptt", {"action": "on", "confirm": True})
        check("未武装就 confirm 被拒", premature.get("isError") is True, why(premature))
        after = body(call("get_state"))
        check("被拒后门禁仍在 rx（证明没偷偷发第一击）",
              after.get("ptt_phase") == "rx" and after.get("ptt_on") is False, after)

        off = call("ptt", {"action": "off"})
        check("从 rx 关发射不报错", not off.get("isError"), why(off))
        check("从 rx 关发射不会把电台按成发射",
              body(off).get("ptt_phase") == "rx" and body(off).get("transmitting") is False,
              body(off))
        check("关发射不需要 confirm，带了就拒",
              call("ptt", {"action": "off", "confirm": True}).get("isError") is True)
        check("log_qso 缺 callsign 回 isError",
              call("log_qso", {"freq_mhz": "14.27"}).get("isError") is True)
        adif0 = call("export_adif")
        check("export_adif 是只读且能取到文本", "adif" in body(adif0), why(adif0))

        send({"jsonrpc": "2.0", "id": 900, "method": "prompts/list"})
        check("不支持的方法给 -32601",
              frames(proc).get("error", {}).get("code") == -32601)

        # ---- 会改电台状态的一段：显式开关 ----
        if not args.allow_tx:
            for label in ("设频/设制式生效", "两击才进 TX", "非业余段两击也拦",
                          "QSO 录入 + ADIF 导出"):
                skip(label)
        else:
            tuned = call("tune", {"freq_hz": 7_100_000})
            check("设频到 7.1 MHz 成功", not tuned.get("isError"), why(tuned))
            check("设频回 tuned/freq_mhz",
                  body(tuned).get("tuned") is True and body(tuned).get("freq_mhz") == 7.1,
                  body(tuned))
            settled = wait_state(lambda s: s.get("freq_hz") == 7_100_000)
            check("总线真的改了频率（等回读）", settled.get("freq_hz") == 7_100_000, settled)
            mode = call("set_mode", {"mode": "USB"})
            check("设制式 USB 生效", body(mode).get("mode") == "USB", why(mode))

            arm = call("ptt", {"action": "on"})
            check("第一击只武装", body(arm).get("ptt_phase") == "confirm_pending"
                  and body(arm).get("transmitting") is False, body(arm))
            check("一次调用只发一次总线动作", body(arm).get("bus_presses_sent") == 1, body(arm))
            check("已武装再不带 confirm 被拒",
                  call("ptt", {"action": "on"}).get("isError") is True)
            check("武装着但没发射（ptt_on 仍是 false）",
                  body(call("get_state")).get("ptt_on") is False, body(call("get_state")))

            fire = call("ptt", {"action": "on", "confirm": True})
            check("第二击进 TX", body(fire).get("ptt_phase") == "tx"
                  and body(fire).get("transmitting") is True, body(fire))
            keyed = wait_state(lambda s: s.get("ptt_on") is True)
            check("总线回读的 ptt_on 跟着变 true（0x1C 读帧真的发出去了）",
                  keyed.get("ptt_on") is True, keyed)
            check("发射中再按 on 被拒（要关用 off）",
                  call("ptt", {"action": "on"}).get("isError") is True)

            stop = call("ptt", {"action": "off"})
            check("关发射落 rx", body(stop).get("ptt_phase") == "rx"
                  and body(stop).get("transmitting") is False, body(stop))
            unkeyed = wait_state(lambda s: s.get("ptt_on") is False)
            check("关发射后总线 ptt_on 回 false", unkeyed.get("ptt_on") is False, unkeyed)

            # 100 MHz 可收听（在 RX 范围内）但不在业余白名单：两击齐了也不许发。
            # 这里**故意不等回读**：设频帧已发、store 还是旧频率的那段窗口才是真危险
            # 的地方（修之前实测 12 轮漏放 10 轮）。等回读再打就把它绕过去了。
            check("调到可收听的非业余段", not call("tune", {"freq_hz": 100_000_000}).get("isError"))
            call("ptt", {"action": "on"})
            blocked = call("ptt", {"action": "on", "confirm": True})
            check("非业余段两击齐了也进不去 TX",
                  body(blocked).get("transmitting") is False
                  and body(blocked).get("ptt_phase") == "rx", body(blocked))
            check("拦下时给了人话 warning",
                  any("业余" in w for w in body(blocked).get("warnings", [])),
                  body(blocked).get("warnings"))
            landed = wait_state(lambda s: s.get("freq_hz") == 100_000_000)
            check("被拦后电台确实停在 100 MHz 且没发射",
                  landed.get("freq_hz") == 100_000_000 and landed.get("ptt_on") is False,
                  landed)
            call("ptt", {"action": "off"})

            logged = call("log_qso", {"callsign": "BG1ABC", "freq_mhz": "14.27",
                                      "mode": "usb", "rst_sent": "59"})
            check("QSO 录入成功", body(logged).get("logged") is True, why(logged))
            check("mode 归一成大写", body(logged).get("qso", {}).get("mode") == "USB",
                  body(logged).get("qso"))
            adif = body(call("export_adif"))
            check("ADIF 里有这条呼号", "<CALL:6>BG1ABC" in adif.get("adif", ""), adif)
            check("ADIF 带 freq/mode/rst", all(k in adif.get("adif", "")
                                              for k in ("<FREQ:", "<MODE:", "<RST_SENT:")), adif)
            check("records 数的是 <EOR>", adif.get("records", 0) >= 1, adif)
    finally:
        if args.allow_tx and original:
            # 还原：smoke 不该把电台留在 100 MHz 上
            if original.get("freq_hz"):
                call("tune", {"freq_hz": int(original["freq_hz"])})
            if original.get("mode"):
                call("set_mode", {"mode": original["mode"]})
            call("ptt", {"action": "off"})
        proc.stdin.close()
        err = proc.stderr.read()
        proc.wait(timeout=10)

    print("\n--- 适配器 stderr（日志只许出现在这里）---")
    print(err.strip() or "(空)")
    tail = f"\n结论：{len(failures)} 项失败"
    if skipped:
        tail += f"，{len(skipped)} 项跳过（--allow-tx 才跑）"
    print(tail + ("" if not failures else " -> " + ", ".join(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())